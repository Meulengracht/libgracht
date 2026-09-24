/**
 * Deterministic regressions for asynchronous use of the client runtime.
 *
 * The link below models a bounded packet transport, but request registration,
 * dispatch, response storage and result finalization all use the real runtime.
 * No private client state is inspected. A two-buffer receive pool makes leaks
 * from ignored responses observable without depending on allocator statistics.
 */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <gracht/client.h>
#include <utils.h>

/* These entry points are also declared by generated client bindings. */
extern int gracht_client_get_buffer(gracht_client_t*, gracht_buffer_t*);
extern int gracht_client_invoke(gracht_client_t*, struct gracht_message_context*, gracht_buffer_t*);
extern int gracht_client_get_status_buffer(gracht_client_t*, struct gracht_message_context*, gracht_buffer_t*);
extern int gracht_client_status_finalize(gracht_client_t*, gracht_buffer_t*);

enum { FRAME_SIZE = GRACHT_MESSAGE_HEADER_SIZE + 4, POOL_SIZE = 256, SERVICE = 61, ACTION = 7};
struct TestLink {
    struct gracht_link Base;
    char Frame[FRAME_SIZE];
    int Ready;
};

static int Send(struct gracht_link* link, gracht_buffer_t* buffer, void* context)
{
    (void)link;
    (void)context;
    assert(buffer->index == GRACHT_MESSAGE_HEADER_SIZE);
    return 0;
}

static int Receive(struct gracht_link* link, gracht_buffer_t* buffer, unsigned int flags)
{
    struct TestLink* test = (struct TestLink*)link;
    (void)flags;
    if (!test->Ready) { errno = EAGAIN; return -1; }
    assert(buffer->index >= FRAME_SIZE);
    memcpy(buffer->data, test->Frame, FRAME_SIZE);
    buffer->index = 0;
    buffer->limit = FRAME_SIZE;
    test->Ready = 0;
    return 0;
}

static gracht_client_t* Create(struct TestLink* link)
{
    gracht_client_configuration_t config;
    gracht_client_t* client = NULL;
    memset(link, 0, sizeof(*link));
    link->Base.type = gracht_link_packet_based;
    link->Base.ops.client.send = Send;
    link->Base.ops.client.recv = Receive;
    gracht_client_configuration_init(&config);
    gracht_client_configuration_set_link(&config, &link->Base);
    gracht_client_configuration_set_max_msg_size(&config, POOL_SIZE);
    /* The runtime allocates backing storage when only its size is supplied. */
    gracht_client_configuration_set_recv_buffer(&config, NULL, 2 * POOL_SIZE);
    assert(gracht_client_create(&config, &client) == 0);
    return client;
}

static struct gracht_message_context Request(gracht_client_t* client)
{
    struct gracht_message_context context = {0};
    gracht_buffer_t buffer;
    assert(gracht_client_get_buffer(client, &buffer) == 0);
    memset(buffer.data, 0, GRACHT_MESSAGE_HEADER_SIZE);
    buffer.data[MSG_INDEX_SID] = SERVICE;
    buffer.data[MSG_INDEX_AID] = ACTION;
    buffer.data[MSG_INDEX_FLG] = MESSAGE_FLAG_SYNC;
    buffer.index = GRACHT_MESSAGE_HEADER_SIZE;
    assert(gracht_client_invoke(client, &context, &buffer) == 0);
    return context;
}

static void Queue(struct TestLink* link, uint32_t id, uint8_t service,
                  uint8_t action, uint8_t type, uint32_t value)
{
    assert(!link->Ready);
    memset(link->Frame, 0, sizeof(link->Frame));
    __gracht_write_u32(link->Frame + MSG_INDEX_ID, id);
    __gracht_write_u32(link->Frame + MSG_INDEX_LEN, FRAME_SIZE);
    link->Frame[MSG_INDEX_SID] = service;
    link->Frame[MSG_INDEX_AID] = action;
    link->Frame[MSG_INDEX_FLG] = type;
    __gracht_write_u32(link->Frame + GRACHT_MESSAGE_HEADER_SIZE, value);
    link->Ready = 1;
}

static void Pending(gracht_client_t* client, struct gracht_message_context* context)
{
    gracht_buffer_t buffer = {0};
    assert(gracht_client_get_status(client, context) == GRACHT_MESSAGE_INPROGRESS);
    assert(gracht_client_get_status_buffer(client, context, &buffer) == GRACHT_MESSAGE_INPROGRESS);
    assert(buffer.data == NULL); // pending inspection transfers no buffer ownership
    assert(gracht_client_get_status(client, context) == GRACHT_MESSAGE_INPROGRESS);
}

static void Result(gracht_client_t* client, struct gracht_message_context* context, uint32_t value)
{
    gracht_buffer_t buffer;
    assert(gracht_client_get_status(client, context) == GRACHT_MESSAGE_COMPLETED);
    assert(gracht_client_get_status(client, context) == GRACHT_MESSAGE_COMPLETED);
    assert(gracht_client_get_status_buffer(client, context, &buffer) == GRACHT_MESSAGE_COMPLETED);
    assert(buffer.limit - buffer.index == sizeof(uint32_t));
    assert(__gracht_read_u32(buffer.data + buffer.index) == value);
    assert(gracht_client_status_finalize(client, &buffer) == 0);
    errno = 0;
    assert(gracht_client_get_status_buffer(client, context, &buffer) == -1);
    assert(errno == ENOENT); // completed results are consumed exactly once
    assert(gracht_client_get_status(client, context) == -1);
    assert(errno == ENOENT);
}

static unsigned int EventCalls;
static void Event(gracht_client_t* client, gracht_buffer_t* buffer)
{
    (void)client;
    assert(__gracht_read_u32(buffer->data + buffer->index) == 99);
    ++EventCalls;
}

int main(int argc, char** argv)
{
    struct TestLink link;
    gracht_client_t* client = Create(&link);
    struct gracht_message_context context = Request(client);
    assert(argc == 2);

    if (!strcmp(argv[1], "pending")) {
        /* Generated *_result helpers must be safe before the response arrives,
         * including multiple checks interleaved with unrelated completions. */
        Pending(client, &context);
        Pending(client, &context);
        struct gracht_message_context other = Request(client);
        Queue(&link, other.message_id, SERVICE, ACTION, MESSAGE_FLAG_RESPONSE, 22);
        assert(gracht_client_wait_message(client, NULL, 0) == 0);
        Pending(client, &context);
        Result(client, &other, 22);
    } else if (!strcmp(argv[1], "correlation")) {
        for (int i = 0; i < 2; ++i) {
            Queue(&link, context.message_id, SERVICE + (i == 0), ACTION + (i == 1), MESSAGE_FLAG_RESPONSE, 77);
            assert(gracht_client_wait_message(client, NULL, 0) == -1);
            assert(errno == EPROTO);
            Pending(client, &context);
        }
    } else if (!strcmp(argv[1], "peer")) {
        for (int i = 0; i < 2; ++i) {
            Queue(&link, context.message_id, SERVICE, ACTION, MESSAGE_FLAG_RESPONSE, 77);
            assert(gracht_client_wait_message(client, NULL, 0) == -1);
            assert(errno == EPROTO);
            Pending(client, &context);
        }
    } else if (!strcmp(argv[1], "duplicate")) {
        Queue(&link, context.message_id, SERVICE, ACTION, MESSAGE_FLAG_RESPONSE, 11);
        assert(gracht_client_wait_message(client, NULL, 0) == 0);
        /* A duplicate must neither replace the first result nor leak the new
         * receive buffer. More repeats than the pool size expose either bug. */
        for (int i = 0; i < 16; ++i) {
            Queue(&link, context.message_id, SERVICE, ACTION, MESSAGE_FLAG_RESPONSE, 88);
            assert(gracht_client_wait_message(client, NULL, 0) == -1);
            assert(errno == EALREADY);
        }
        Result(client, &context, 11);
        gracht_client_shutdown(client);
        return 0;
    } else if (!strcmp(argv[1], "abandon")) {
        assert(gracht_client_abandon(client, &context) == 0);
        assert(gracht_client_get_status(client, &context) == -1 && errno == ENOENT);
        Queue(&link, context.message_id, SERVICE, ACTION, MESSAGE_FLAG_RESPONSE, 99);
        assert(gracht_client_wait_message(client, NULL, 0) == -1 && errno == ENOENT);
        // Completed-but-unconsumed results must also return pool capacity.
        for (int i = 0; i < 16; ++i) {
            context = Request(client);
            Queue(&link, context.message_id, SERVICE, ACTION, MESSAGE_FLAG_RESPONSE, 99);
            assert(gracht_client_wait_message(client, NULL, 0) == 0);
            assert(gracht_client_abandon(client, &context) == 0);
        }
        context = Request(client);
    } else if (!strcmp(argv[1], "unknown")) {
        for (int i = 0; i < 16; ++i) {
            Queue(&link, context.message_id + 100, SERVICE, ACTION, MESSAGE_FLAG_RESPONSE, 77);
            assert(gracht_client_wait_message(client, NULL, 0) == -1);
            assert(errno == ENOENT);
        }
        Pending(client, &context);
    } else if (!strcmp(argv[1], "event")) {
        gracht_protocol_function_t function = {ACTION, (void*)Event};
        gracht_protocol_t protocol = {.id = SERVICE, .name = "runtime-test", .num_functions = 1, .functions = &function};
        assert(gracht_client_register_protocol(client, &protocol) == 0);
        Queue(&link, 0, SERVICE, ACTION, MESSAGE_FLAG_EVENT, 99);
        assert(gracht_client_wait_message(client, NULL, 0) == 0);
        assert(EventCalls == 1);
        Pending(client, &context);
    } else {
        assert(!"unknown test scenario");
    }

    Queue(&link, context.message_id, SERVICE, ACTION, MESSAGE_FLAG_RESPONSE, 11);
    assert(gracht_client_wait_message(client, NULL, 0) == 0);
    Result(client, &context, 11);
    gracht_client_shutdown(client);
    puts("client runtime regression passed");
    return 0;
}
