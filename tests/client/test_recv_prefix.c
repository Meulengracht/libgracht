// Receive metadata must not reduce the configured wire capacity or be lost
// during response ownership transfer and event dispatch.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <gracht/client.h>
#include <utils.h>

extern int gracht_client_get_buffer(gracht_client_t*, gracht_buffer_t*);
extern int gracht_client_invoke(gracht_client_t*, struct gracht_message_context*, gracht_buffer_t*);

enum { FRAME_SIZE = 256, STREAM_SIZE = 512, PREFIX_SIZE = 16, SERVICE = 61, ACTION = 7 };

struct TestLink {
    struct gracht_link Base;
    uint32_t MessageId;
    uint32_t FrameSize;
    uint8_t Type;
    int Receives;
};

static int
__Send(struct gracht_link* link, gracht_buffer_t* buffer, void* context)
{
    (void)context;
    ((struct TestLink*)link)->MessageId = GB_MSG_ID_0(buffer);
    return 0;
}

static int
__Receive(struct gracht_link* link, gracht_buffer_t* buffer, unsigned int flags)
{
    struct TestLink* test = (struct TestLink*)link;
    uint32_t prefix = link->ops.client.msg_header_size;
    char* frame = buffer->data + prefix;
    (void)flags;

    assert(buffer->index == test->FrameSize + prefix);
    memset(buffer->data, 0x5a, prefix);
    memset(frame, 0x31, test->FrameSize);
    __gracht_write_u32(frame + MSG_INDEX_ID, test->MessageId);
    __gracht_write_u32(frame + MSG_INDEX_LEN, test->FrameSize);
    frame[MSG_INDEX_SID] = SERVICE;
    frame[MSG_INDEX_AID] = ACTION;
    frame[MSG_INDEX_FLG] = test->Type;
    buffer->index = prefix;
    buffer->limit = prefix + test->FrameSize;
    ++test->Receives;
    return 0;
}

static int
__Peek(struct gracht_link* link, uint32_t* size, uint8_t* service, unsigned int flags)
{
    (void)flags;
    *size = ((struct TestLink*)link)->FrameSize;
    *service = SERVICE;
    return 0;
}

static void
__CheckBuffer(gracht_buffer_t* buffer, uint32_t prefix, uint32_t frameSize)
{
    uint32_t i;

    assert(buffer->index == prefix + GRACHT_MESSAGE_HEADER_SIZE);
    assert(buffer->limit == prefix + frameSize);
    for (i = 0; i < prefix; ++i) {
        assert((unsigned char)buffer->data[i] == 0x5a);
    }
    for (i = buffer->index; i < buffer->limit; ++i) {
        assert(buffer->data[i] == 0x31);
    }
}

static int EventCalls;

static void
__Event(gracht_client_t* client, gracht_buffer_t* buffer)
{
    (void)client;
    __CheckBuffer(buffer, PREFIX_SIZE, FRAME_SIZE);
    ++EventCalls;
}

static void
__Request(gracht_client_t* client, struct gracht_message_context* context,
          int stream, uint32_t responseSize)
{
    gracht_buffer_t buffer;

    if (stream) {
        assert(gracht_client_get_stream_buffer(client, &buffer) == 0);
        assert(buffer.limit == STREAM_SIZE);
    } else {
        assert(gracht_client_get_buffer(client, &buffer) == 0);
        assert(buffer.limit == FRAME_SIZE);
    }
    memset(buffer.data, 0, GRACHT_MESSAGE_HEADER_SIZE);
    buffer.data[MSG_INDEX_SID] = SERVICE;
    buffer.data[MSG_INDEX_AID] = ACTION;
    buffer.data[MSG_INDEX_FLG] = MESSAGE_FLAG_SYNC;
    buffer.index = GRACHT_MESSAGE_HEADER_SIZE;
    if (stream) {
        assert(gracht_client_invoke_stream_sized(client, context, &buffer, responseSize) == 0);
    } else {
        assert(gracht_client_invoke(client, context, &buffer) == 0);
    }
}

static void
__Run(uint32_t prefix, int suppliedStorage, int stream, int peek, int sized)
{
    struct TestLink link = {0};
    gracht_client_configuration_t config;
    gracht_client_t* client;
    struct gracht_message_context contexts[2] = {{0}, {0}};
    gracht_buffer_t buffers[2];
    unsigned char storage[2 * (FRAME_SIZE + PREFIX_SIZE) + 1];
    gracht_protocol_function_t function = {ACTION, (void*)__Event};
    gracht_protocol_t protocol = {.id = SERVICE, .name = "prefix-test", .num_functions = 1, .functions = &function};
    int i;

    memset(storage, 0xa5, sizeof(storage));
    link.Base.type = gracht_link_packet_based;
    link.Base.ops.client.send = __Send;
    link.Base.ops.client.recv = __Receive;
    link.Base.ops.client.peek = peek ? __Peek : NULL;
    link.Base.ops.client.msg_header_size = prefix;
    link.FrameSize = stream ? STREAM_SIZE : FRAME_SIZE;
    link.Type = MESSAGE_FLAG_RESPONSE;
    gracht_client_configuration_init(&config);
    gracht_client_configuration_set_link(&config, &link.Base);
    gracht_client_configuration_set_max_msg_size(&config, FRAME_SIZE);
    gracht_client_configuration_set_stream_buffer_size(&config, STREAM_SIZE, 2);
    gracht_client_configuration_set_recv_buffer(&config, suppliedStorage ? storage : NULL,
        2 * (FRAME_SIZE + (suppliedStorage ? prefix : 0)));
    assert(gracht_client_create(&config, &client) == 0);

    // Hold two maximum-size replies concurrently, then release both. This also
    // checks slot stride and that internally allocated pools retain their count.
    for (i = 0; i < 2; ++i) {
        __Request(client, &contexts[i], stream, sized ? STREAM_SIZE : 0);
        assert(gracht_client_wait_message(client, &contexts[i], 0) == 0);
        assert(gracht_client_get_status_buffer(client, &contexts[i], &buffers[i]) == GRACHT_MESSAGE_COMPLETED);
        __CheckBuffer(&buffers[i], prefix, link.FrameSize);
    }
    for (i = 0; i < 2; ++i) {
        __CheckBuffer(&buffers[i], prefix, link.FrameSize);
        assert(gracht_client_status_finalize(client, &buffers[i]) == 0);
    }
    assert(storage[sizeof(storage) - 1] == 0xa5);

    if (!stream && prefix == PREFIX_SIZE) {
        assert(gracht_client_register_protocol(client, &protocol) == 0);
        link.Type = MESSAGE_FLAG_EVENT;
        EventCalls = 0;
        for (i = 0; i < 4; ++i) {
            assert(gracht_client_wait_message(client, NULL, 0) == 0);
        }
        assert(EventCalls == 4);
    }
    gracht_client_shutdown(client);
}

static void
__InvalidSizes(void)
{
    struct TestLink link = {0};
    gracht_client_configuration_t config;
    gracht_client_t* client = NULL;
    struct gracht_message_context context = {0};
    unsigned char storage[FRAME_SIZE + PREFIX_SIZE - 1];

    link.Base.ops.client.send = __Send;
    link.Base.ops.client.recv = __Receive;
    link.Base.ops.client.msg_header_size = PREFIX_SIZE;
    gracht_client_configuration_init(&config);
    gracht_client_configuration_set_link(&config, &link.Base);
    gracht_client_configuration_set_max_msg_size(&config, FRAME_SIZE);
    gracht_client_configuration_set_recv_buffer(&config, storage, sizeof(storage));
    assert(gracht_client_create(&config, &client) == -1 && errno == EINVAL);
    assert(client == NULL);

    config.recv_buffer = NULL;
    link.Base.ops.client.msg_header_size = UINT32_MAX;
    assert(gracht_client_create(&config, &client) == -1 && errno == EOVERFLOW);

    // A normalized stream frame fits uint32_t, but its metadata does not.
    link.Base.ops.client.msg_header_size = 256;
    gracht_client_configuration_set_stream_buffer_size(&config, STREAM_SIZE, 2);
    assert(gracht_client_create(&config, &client) == 0);
    __Request(client, &context, 1, UINT32_MAX - 255);
    assert(gracht_client_wait_message(client, &context, 0) == -1 && errno == EOVERFLOW);
    assert(link.Receives == 0);
    assert(gracht_client_abandon(client, &context) == 0);
    gracht_client_shutdown(client);
}

int main(void)
{
    __Run(0, 0, 0, 0, 0);
    __Run(PREFIX_SIZE, 0, 0, 0, 0);
    __Run(PREFIX_SIZE, 1, 0, 0, 0);
    __Run(PREFIX_SIZE, 0, 0, 1, 0);
    __Run(PREFIX_SIZE, 0, 1, 0, 0);
    __Run(PREFIX_SIZE, 0, 1, 0, 1);
    __Run(PREFIX_SIZE, 0, 1, 1, 1);
    __InvalidSizes();
    return 0;
}
