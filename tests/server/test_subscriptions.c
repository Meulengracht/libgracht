/** Broadcast events only reach clients subscribed to the event's 16 bit protocol id. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <gracht/server.h>
#include <assert.h>
#include <control.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <utils.h>

extern int gracht_server_get_buffer(gracht_server_t*, gracht_buffer_t*);
extern int gracht_server_broadcast_event(gracht_server_t*, gracht_buffer_t*, unsigned int);

enum {
    PACKET_LINK = 42,
    STREAM_LINK = 43,
    PACKET_CLIENT = 7,
    STREAM_CLIENT = 9,
    // Two protocol ids that only differ in the upper byte. An 8 bit id could
    // not tell them apart.
    PROTOCOL = 0x1234,
    PROTOCOL_SAME_LOW_BYTE = 0x0534,
    OTHER_PROTOCOL = 0x2000,
    // The size of a subscribe or unsubscribe request. It is the header
    // followed by the 16 bit protocol id.
    REQUEST_SIZE = GRACHT_MESSAGE_HEADER_SIZE + 2
};

// What a test client has been sent by the server.
struct TestClient {
    struct gracht_server_client Base;
    unsigned                    Events;
    unsigned                    Errors;
    uint32_t                    ErrorMessageId;
    int                         ErrorCode;
};

// The next control request the links hand to the server.
struct PendingRequest {
    int           Ready;
    gracht_conn_t Client;
    uint32_t      MessageId;
    uint8_t       Action;
    uint16_t      Protocol;
};

static struct PendingRequest g_request;
static struct TestClient*    g_packetClient;
static struct TestClient*    g_streamClient;
static unsigned              g_destroyed;

static gracht_conn_t
__SetupPacket(struct gracht_link* link, gracht_handle_t set)
{
    (void)set;
    link->connection = PACKET_LINK;
    return PACKET_LINK;
}

static gracht_conn_t
__SetupStream(struct gracht_link* link, gracht_handle_t set)
{
    (void)set;
    link->connection = STREAM_LINK;
    return STREAM_LINK;
}

static void
__DestroyLink(struct gracht_link* link, gracht_handle_t set)
{
    (void)link;
    (void)set;
}

static struct TestClient*
__NewClient(gracht_conn_t handle)
{
    // Links hand out zeroed clients. The server relies on that for the
    // subscription list.
    struct TestClient* client = calloc(1, sizeof(struct TestClient));

    assert(client);
    client->Base.handle = handle;
    return client;
}

static int
__CreateClient(struct gracht_link* link, struct gracht_message* message, struct gracht_server_client** clientOut)
{
    (void)link;
    g_packetClient = __NewClient(message->client);
    *clientOut = &g_packetClient->Base;
    return 0;
}

static int
__AcceptClient(struct gracht_link* link, gracht_handle_t set, struct gracht_server_client** clientOut)
{
    (void)link;
    (void)set;
    g_streamClient = __NewClient(STREAM_CLIENT);
    *clientOut = &g_streamClient->Base;
    return 0;
}

static int
__DestroyClient(struct gracht_server_client* client, gracht_handle_t set)
{
    (void)set;
    // Either client can be gone already, so check the pointer before taking
    // the address of its member.
    if (g_packetClient != NULL && client == &g_packetClient->Base) {
        g_packetClient = NULL;
    } else if (g_streamClient != NULL && client == &g_streamClient->Base) {
        g_streamClient = NULL;
    } else {
        assert(!"destroying a client the test did not create");
    }
    free(client);
    g_destroyed++;
    return 0;
}

// Fills the message with the pending subscribe or unsubscribe request.
static int
__TakeRequest(struct gracht_message* message)
{
    if (!g_request.Ready) {
        errno = EAGAIN;
        return -1;
    }
    g_request.Ready = 0;

    message->client = g_request.Client;
    message->link = PACKET_LINK;
    message->size = REQUEST_SIZE;
    message->index = 0;
    memset(message->payload, 0, REQUEST_SIZE);
    __gracht_write_u32(message->payload + MSG_INDEX_ID, g_request.MessageId);
    __gracht_write_u32(message->payload + MSG_INDEX_LEN, REQUEST_SIZE);
    __gracht_write_u16(message->payload + MSG_INDEX_SID, SERVICE_GRACHT_CONTROL_ID);
    message->payload[MSG_INDEX_AID] = g_request.Action;
    message->payload[MSG_INDEX_FLG] = MESSAGE_FLAG_ASYNC;
    __gracht_write_u16(message->payload + GRACHT_MESSAGE_HEADER_SIZE, g_request.Protocol);
    return 0;
}

static int
__ReceivePacket(struct gracht_link* link, struct gracht_message* message, unsigned int flags)
{
    (void)link;
    (void)flags;
    return __TakeRequest(message);
}

static int
__ReceiveClient(struct gracht_server_client* client, struct gracht_message* message, unsigned int flags)
{
    (void)client;
    (void)flags;
    return __TakeRequest(message);
}

// Records what the server sends to a client. This is either a broadcast event
// or the control protocol's error event.
static int
__SendClient(struct gracht_server_client* base, struct gracht_buffer* buffer, unsigned int flags)
{
    struct TestClient* client = (struct TestClient*)base;
    uint16_t           protocol = __gracht_read_u16(buffer->data + MSG_INDEX_SID);
    (void)flags;

    if (protocol != SERVICE_GRACHT_CONTROL_ID) {
        client->Events++;
        return 0;
    }

    assert((uint8_t)buffer->data[MSG_INDEX_AID] == SERVICE_GRACHT_CONTROL_EVENT_ERROR_ID);
    client->Errors++;
    client->ErrorMessageId = __gracht_read_u32(buffer->data + GRACHT_MESSAGE_HEADER_SIZE);
    memcpy(&client->ErrorCode, buffer->data + GRACHT_MESSAGE_HEADER_SIZE + sizeof(uint32_t), sizeof(int));
    return 0;
}

// Sends a subscribe or unsubscribe request to the server on behalf of a client.
// Packet clients send through the link. Stream clients have their own handle.
static void
__Request(gracht_server_t* server, gracht_conn_t client, uint8_t action, uint16_t protocol)
{
    g_request.Ready = 1;
    g_request.Client = client;
    g_request.MessageId++;
    g_request.Action = action;
    g_request.Protocol = protocol;
    assert(gracht_server_handle_event(server, client == STREAM_CLIENT ? STREAM_CLIENT : PACKET_LINK, 0) == 0);
    assert(!g_request.Ready);
}

static void
__Subscribe(gracht_server_t* server, gracht_conn_t client, uint16_t protocol)
{
    __Request(server, client, SERVICE_GRACHT_CONTROL_SUBSCRIBE_ID, protocol);
}

static void
__Unsubscribe(gracht_server_t* server, gracht_conn_t client, uint16_t protocol)
{
    __Request(server, client, SERVICE_GRACHT_CONTROL_UNSUBSCRIBE_ID, protocol);
}

// Broadcasts one event of the protocol. Returns whether the client was sent it.
static int
__Receives(gracht_server_t* server, struct TestClient* client, uint16_t protocol)
{
    gracht_buffer_t buffer;
    unsigned        before = client->Events;

    assert(gracht_server_get_buffer(server, &buffer) == 0);
    memset(buffer.data, 0, GRACHT_MESSAGE_HEADER_SIZE);
    __gracht_write_u16(buffer.data + MSG_INDEX_SID, protocol);
    buffer.data[MSG_INDEX_FLG] = MESSAGE_FLAG_EVENT;
    buffer.index = GRACHT_MESSAGE_HEADER_SIZE;
    assert(gracht_server_broadcast_event(server, &buffer, 0) == 0);
    assert(client->Events - before <= 1);
    return client->Events != before;
}

// A packet client starts without subscriptions and adds them one at a time.
static void
__TestListedProtocols(gracht_server_t* server)
{
    __Subscribe(server, PACKET_CLIENT, PROTOCOL);
    assert(g_packetClient != NULL);
    assert(__Receives(server, g_packetClient, PROTOCOL));
    assert(!__Receives(server, g_packetClient, PROTOCOL_SAME_LOW_BYTE));
    assert(!__Receives(server, g_packetClient, OTHER_PROTOCOL));

    // Subscribing twice must not use up a second entry of the list.
    __Subscribe(server, PACKET_CLIENT, PROTOCOL);
    assert(g_packetClient->Base.subscriptions.count == 1);

    __Subscribe(server, PACKET_CLIENT, OTHER_PROTOCOL);
    __Unsubscribe(server, PACKET_CLIENT, PROTOCOL);
    assert(!__Receives(server, g_packetClient, PROTOCOL));
    assert(__Receives(server, g_packetClient, OTHER_PROTOCOL));
    __Unsubscribe(server, PACKET_CLIENT, OTHER_PROTOCOL);
    assert(g_packetClient->Base.subscriptions.count == 0);
    assert(g_packetClient->Errors == 0);
}

// A full list rejects new protocols, and the client is told through the
// control protocol's error event.
static void
__TestFullList(gracht_server_t* server)
{
    uint16_t firstProtocol = 0x4000;
    uint16_t rejectedProtocol = firstProtocol + GRACHT_CLIENT_MAX_SUBSCRIPTIONS;
    uint16_t protocol;

    for (protocol = firstProtocol; protocol < rejectedProtocol; protocol++) {
        __Subscribe(server, PACKET_CLIENT, protocol);
        assert(__Receives(server, g_packetClient, protocol));
    }
    assert(g_packetClient->Errors == 0);

    __Subscribe(server, PACKET_CLIENT, rejectedProtocol);
    assert(g_packetClient->Errors == 1);
    assert(g_packetClient->ErrorMessageId == g_request.MessageId);
    assert(g_packetClient->ErrorCode == ENOSPC);
    assert(!__Receives(server, g_packetClient, rejectedProtocol));

    // The protocols already in the list are not affected.
    __Subscribe(server, PACKET_CLIENT, firstProtocol);
    assert(g_packetClient->Errors == 1);
    assert(__Receives(server, g_packetClient, firstProtocol));

    // Removing one protocol makes room for the rejected one.
    __Unsubscribe(server, PACKET_CLIENT, firstProtocol);
    __Subscribe(server, PACKET_CLIENT, rejectedProtocol);
    assert(g_packetClient->Errors == 1);
    assert(!__Receives(server, g_packetClient, firstProtocol));
    assert(__Receives(server, g_packetClient, rejectedProtocol));
}

// GRACHT_PROTOCOL_ID_ALL subscribes to every protocol. Unsubscribing a single
// protocol afterwards only removes that one.
static void
__TestAllProtocols(gracht_server_t* server)
{
    __Subscribe(server, PACKET_CLIENT, GRACHT_PROTOCOL_ID_ALL);
    assert(g_packetClient->Base.subscriptions.count == 0);
    assert(__Receives(server, g_packetClient, PROTOCOL));
    assert(__Receives(server, g_packetClient, PROTOCOL_SAME_LOW_BYTE));

    __Unsubscribe(server, PACKET_CLIENT, PROTOCOL);
    assert(!__Receives(server, g_packetClient, PROTOCOL));
    assert(__Receives(server, g_packetClient, PROTOCOL_SAME_LOW_BYTE));

    __Subscribe(server, PACKET_CLIENT, PROTOCOL);
    assert(__Receives(server, g_packetClient, PROTOCOL));
    assert(g_packetClient->Base.subscriptions.count == 0);

    // A packet client that unsubscribes from everything is removed.
    __Unsubscribe(server, PACKET_CLIENT, GRACHT_PROTOCOL_ID_ALL);
    assert(g_packetClient == NULL && g_destroyed == 1);
}

// A stream client is subscribed to every protocol from the moment it connects.
static void
__TestStreamClient(gracht_server_t* server)
{
    assert(gracht_server_handle_event(server, STREAM_LINK, 0) == 0);
    assert(g_streamClient != NULL);
    assert(__Receives(server, g_streamClient, PROTOCOL));

    __Unsubscribe(server, STREAM_CLIENT, PROTOCOL);
    assert(!__Receives(server, g_streamClient, PROTOCOL));
    assert(__Receives(server, g_streamClient, PROTOCOL_SAME_LOW_BYTE));

    // Unsubscribing from everything leaves the connection open.
    __Unsubscribe(server, STREAM_CLIENT, GRACHT_PROTOCOL_ID_ALL);
    assert(g_streamClient != NULL);
    assert(!__Receives(server, g_streamClient, PROTOCOL_SAME_LOW_BYTE));

    __Subscribe(server, STREAM_CLIENT, PROTOCOL);
    assert(__Receives(server, g_streamClient, PROTOCOL));
    assert(!__Receives(server, g_streamClient, PROTOCOL_SAME_LOW_BYTE));
    assert(g_streamClient->Errors == 0);
}

int
main(void)
{
    gracht_server_configuration_t configuration;
    gracht_server_t*              server;
    struct gracht_link            packetLink = {.type = gracht_link_packet_based};
    struct gracht_link            streamLink = {.type = gracht_link_stream_based};

    gracht_server_configuration_init(&configuration);
    gracht_server_configuration_set_num_workers(&configuration, 0);
    assert(gracht_server_create(&configuration, &server) == 0);

    packetLink.ops.server.setup = __SetupPacket;
    packetLink.ops.server.destroy = __DestroyLink;
    packetLink.ops.server.create_client = __CreateClient;
    packetLink.ops.server.destroy_client = __DestroyClient;
    packetLink.ops.server.send_client = __SendClient;
    packetLink.ops.server.recv = __ReceivePacket;
    assert(gracht_server_add_link(server, &packetLink) == 0);

    streamLink.ops.server.setup = __SetupStream;
    streamLink.ops.server.destroy = __DestroyLink;
    streamLink.ops.server.accept_client = __AcceptClient;
    streamLink.ops.server.destroy_client = __DestroyClient;
    streamLink.ops.server.send_client = __SendClient;
    streamLink.ops.server.recv_client = __ReceiveClient;
    assert(gracht_server_add_link(server, &streamLink) == 0);

    __TestListedProtocols(server);
    __TestFullList(server);
    __TestAllProtocols(server);
    __TestStreamClient(server);

    gracht_server_request_shutdown(server);
    assert(gracht_server_handle_event(server, PACKET_LINK, 0) == -1 && errno == EPIPE);
    assert(g_streamClient == NULL && g_destroyed == 2);
    return 0;
}
