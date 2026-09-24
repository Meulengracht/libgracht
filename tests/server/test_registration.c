/** Packet senders can be registered at application open, without subscribe IPC. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <gracht/server.h>
#include <utils.h>
extern int gracht_server_get_buffer(gracht_server_t*, gracht_buffer_t*);
extern int gracht_server_send_event(gracht_server_t*, gracht_conn_t, gracht_buffer_t*, unsigned int);
static unsigned created, destroyed, sent;
static int failCreate, receiveReady;
static gracht_conn_t Setup(struct gracht_link* link, gracht_handle_t set)
{ (void)set; link->connection=42; return 42; }
static void DestroyLink(struct gracht_link* link, gracht_handle_t set) { (void)link; (void)set; }
static int Create(struct gracht_link* link, struct gracht_message* message, struct gracht_server_client** out)
{
    (void)link;
    if(failCreate) { errno=ENOMEM; return -1; }
    *out=calloc(1,sizeof(**out)); assert(*out); (*out)->handle=message->client; created++; return 0;
}
static int Destroy(struct gracht_server_client* client, gracht_handle_t set)
{ (void)set; free(client); destroyed++; return 0; }
static int Send(struct gracht_server_client* client, gracht_buffer_t* buffer, unsigned int flags)
{ (void)flags; assert(client->handle==7 && buffer->index==GRACHT_MESSAGE_HEADER_SIZE); sent++; return 0; }
static int Receive(struct gracht_server_client* client, struct gracht_message* message, unsigned int flags)
{
    (void)flags;
    if (!receiveReady) { errno=EAGAIN; return -1; }
    receiveReady=0;
    message->client=client->handle; message->link=42; message->size=12; message->index=0;
    memset(message->payload,0,12);
    __gracht_write_u32(message->payload+4,12);
    message->payload[10]=MESSAGE_FLAG_ASYNC; // control/subscribe protocol 0, action 0
    message->payload[11]=61;
    return 0;
}
static int Event(gracht_server_t* server)
{
    gracht_buffer_t buffer;
    assert(!gracht_server_get_buffer(server,&buffer));
    memset(buffer.data,0,GRACHT_MESSAGE_HEADER_SIZE);
    buffer.index=GRACHT_MESSAGE_HEADER_SIZE;
    return gracht_server_send_event(server,7,&buffer,0);
}
int main(void)
{
    gracht_server_t* server;
    gracht_server_configuration_t config;
    gracht_server_configuration_init(&config);
    gracht_server_configuration_set_num_workers(&config,0);
    assert(!gracht_server_create(&config,&server));
    struct gracht_link link={.type=gracht_link_packet_based};
    link.ops.server.setup=Setup; link.ops.server.destroy=DestroyLink;
    link.ops.server.create_client=Create; link.ops.server.destroy_client=Destroy;
    link.ops.server.send_client=Send; link.ops.server.recv_client=Receive;
    assert(!gracht_server_add_link(server,&link));
    // Exercise failed directed sends repeatedly; their send buffers must also be
    // returned when no client entry exists (checked under leak sanitizer).
    for(unsigned i=0;i<32;++i) assert(Event(server)==-1 && errno==ENOENT);
    struct gracht_message message={.server=server,.client=7,.link=42};
    failCreate=1; assert(gracht_server_register_client(&message)==-1 && errno==ENOMEM);
    failCreate=0; assert(!gracht_server_register_client(&message));
    assert(!gracht_server_register_client(&message) && created==1);
    // Client dispatch holds a read lock while invoking a subscription handler.
    // Registering an existing client must not attempt a write-lock upgrade.
    receiveReady=1;
    assert(!gracht_server_handle_event(server,7,0) && created==1);
    assert(!Event(server) && sent==1);
    message.client=8; message.link=99;
    assert(gracht_server_register_client(&message)==-1 && errno==EINVAL);
    assert(gracht_server_register_client(NULL)==-1 && errno==EINVAL);
    gracht_server_request_shutdown(server);
    assert(gracht_server_handle_event(server,42,0)==-1 && errno==EPIPE);
    assert(destroyed==1);
    return 0;
}
