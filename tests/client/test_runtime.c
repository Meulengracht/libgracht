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
#include <control.h>
#include <utils.h>

/* These entry points are also declared by generated client bindings. */
extern int gracht_client_get_buffer(gracht_client_t*, gracht_buffer_t*);
extern int gracht_client_invoke(gracht_client_t*, struct gracht_message_context*, gracht_buffer_t*);
extern int gracht_client_get_status_buffer(gracht_client_t*, struct gracht_message_context*, gracht_buffer_t*);
extern int gracht_client_status_finalize(gracht_client_t*, gracht_buffer_t*);

enum {
    FRAME_SIZE = GRACHT_MESSAGE_HEADER_SIZE + 4,
    /* A control error event carries a message id and an error code. */
    ERROR_FRAME_SIZE = GRACHT_MESSAGE_HEADER_SIZE + 8,
    POOL_SIZE = 256,
    SERVICE = 0x1234,
    ACTION = 7,
    PEER = 42,
    IDLE_LIMIT = 64
};
struct TestLink {
    struct gracht_link Base;
    char Frame[ERROR_FRAME_SIZE];
    uint32_t Length;
    uint32_t Sender;
    int Ready;
    /* While set, every receive checks that the waited-on request cannot be
     * abandoned, because the caller is still waiting on it. */
    gracht_client_t* Client;
    struct gracht_message_context* Waiting;
    /* The errno of a receive when no frame is ready. A synchronous wait must end
     * with it instead of trying again. Idle counts those receives, and a wait
     * that keeps trying is stopped after IDLE_LIMIT of them. */
    int IdleError;
    int Idle;
    /* Called after a frame was delivered, so a test can queue the next one. */
    void (*Delivered)(struct TestLink* link);
    int Deliveries;
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
    if (test->Waiting) {
        errno = 0;
        assert(gracht_client_abandon(test->Client, test->Waiting) == -1);
        assert(errno == EBUSY);
    }
    if (!test->Ready) {
        assert(++test->Idle < IDLE_LIMIT);
        errno = test->IdleError ? test->IdleError : EAGAIN;
        return -1;
    }
    test->Ready = 0;
    // Model peer validation in the transport. The generic runtime does not
    // interpret transport-specific sender metadata or authorize responses.
    if (test->Sender != PEER) {
        errno = EPROTO;
        return -1;
    }
    assert(buffer->index >= test->Length);
    memcpy(buffer->data, test->Frame, test->Length);
    buffer->index = 0;
    buffer->limit = test->Length;
    test->Deliveries++;
    if (test->Delivered) {
        test->Delivered(test);
    }
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
    __gracht_write_u16(buffer.data + MSG_INDEX_SID, SERVICE);
    buffer.data[MSG_INDEX_AID] = ACTION;
    buffer.data[MSG_INDEX_FLG] = MESSAGE_FLAG_SYNC;
    buffer.index = GRACHT_MESSAGE_HEADER_SIZE;
    assert(gracht_client_invoke(client, &context, &buffer) == 0);
    return context;
}

static void Queue(struct TestLink* link, uint32_t id, uint16_t service,
                  uint8_t action, uint8_t type, uint32_t value)
{
    assert(!link->Ready);
    memset(link->Frame, 0, sizeof(link->Frame));
    __gracht_write_u32(link->Frame + MSG_INDEX_ID, id);
    __gracht_write_u32(link->Frame + MSG_INDEX_LEN, FRAME_SIZE);
    __gracht_write_u16(link->Frame + MSG_INDEX_SID, service);
    link->Frame[MSG_INDEX_AID] = action;
    link->Frame[MSG_INDEX_FLG] = type;
    __gracht_write_u32(link->Frame + GRACHT_MESSAGE_HEADER_SIZE, value);
    link->Length = FRAME_SIZE;
    link->Sender = PEER;
    link->Ready = 1;
}

/* Queues the control error event a server sends when it cannot run a call. */
static void QueueError(struct TestLink* link, uint32_t id)
{
    assert(!link->Ready);
    memset(link->Frame, 0, sizeof(link->Frame));
    __gracht_write_u32(link->Frame + MSG_INDEX_LEN, ERROR_FRAME_SIZE);
    __gracht_write_u16(link->Frame + MSG_INDEX_SID, SERVICE_GRACHT_CONTROL_ID);
    link->Frame[MSG_INDEX_AID] = SERVICE_GRACHT_CONTROL_EVENT_ERROR_ID;
    link->Frame[MSG_INDEX_FLG] = MESSAGE_FLAG_EVENT;
    __gracht_write_u32(link->Frame + GRACHT_MESSAGE_HEADER_SIZE, id);
    __gracht_write_u32(link->Frame + GRACHT_MESSAGE_HEADER_SIZE + 4, EIO);
    link->Length = ERROR_FRAME_SIZE;
    link->Sender = PEER;
    link->Ready = 1;
}

/* The request is gone: neither its status nor an abandon finds it. */
static void Gone(gracht_client_t* client, struct gracht_message_context* context)
{
    errno = 0;
    assert(gracht_client_get_status(client, context) == -1 && errno == ENOENT);
    errno = 0;
    assert(gracht_client_abandon(client, context) == -1 && errno == ENOENT);
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

/* Answers for the synchronous await cases, queued one per receive. */
static struct gracht_message_context* Answers[3];
static int AnswerCount;
static void QueueNextAnswer(struct TestLink* link)
{
    if (AnswerCount == 0) {
        link->Delivered = NULL;
        return;
    }
    Queue(link, Answers[0]->message_id, SERVICE, ACTION, MESSAGE_FLAG_RESPONSE,
          Answers[0]->message_id);
    memmove(&Answers[0], &Answers[1], sizeof(Answers[0]) * 2);
    AnswerCount--;
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
        // A transport-rejected peer must not complete the request or leak its
        // receive slot. Exceed the two-slot pool, then accept the valid reply
        // queued below once the transport admits the expected peer.
        for (int i = 0; i < 16; ++i) {
            Queue(&link, context.message_id, SERVICE, ACTION, MESSAGE_FLAG_RESPONSE, 77);
            link.Sender = PEER + 1 + i;
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
    } else if (!strcmp(argv[1], "await-error-abandon")) {
        /* An error answer ends the wait with GRACHT_MESSAGE_ERROR. The wait is
         * over, so the caller can abandon the request, and nothing is left. More
         * rounds than the two-slot pool show that no receive buffer is kept. */
        for (int i = 0; i < 16; ++i) {
            QueueError(&link, context.message_id);
            assert(gracht_client_await(client, &context, 0) == 0);
            assert(gracht_client_get_status(client, &context) == GRACHT_MESSAGE_ERROR);
            assert(gracht_client_abandon(client, &context) == 0);
            Gone(client, &context);
            context = Request(client);
        }
    } else if (!strcmp(argv[1], "await-result")) {
        /* An answer that is read with the _result call leaves nothing behind. */
        for (int i = 0; i < 16; ++i) {
            Queue(&link, context.message_id, SERVICE, ACTION, MESSAGE_FLAG_RESPONSE, 33);
            assert(gracht_client_await(client, &context, 0) == 0);
            Result(client, &context, 33);
            Gone(client, &context);
            context = Request(client);
        }
    } else if (!strcmp(argv[1], "await-busy")) {
        /* While the caller still waits, the request cannot be abandoned. Receive
         * checks this every time the wait pumps the link. Once the wait is over,
         * the abandon works, for an answer and for an error answer. */
        link.Client = client;
        link.Waiting = &context;
        Queue(&link, context.message_id, SERVICE, ACTION, MESSAGE_FLAG_RESPONSE, 44);
        assert(gracht_client_await(client, &context, 0) == 0);
        link.Waiting = NULL;
        assert(gracht_client_abandon(client, &context) == 0);
        Gone(client, &context);

        context = Request(client);
        link.Waiting = &context;
        QueueError(&link, context.message_id);
        assert(gracht_client_await(client, &context, 0) == 0);
        link.Waiting = NULL;
        assert(gracht_client_abandon(client, &context) == 0);
        Gone(client, &context);
        context = Request(client);
    } else if (!strcmp(argv[1], "await-multiple")) {
        /* One wait on two requests: the first is answered before the wait, the
         * second gets an error answer during it. Both can be abandoned after. */
        struct gracht_message_context second = Request(client);
        struct gracht_message_context* both[] = {&context, &second};
        Queue(&link, context.message_id, SERVICE, ACTION, MESSAGE_FLAG_RESPONSE, 55);
        assert(gracht_client_wait_message(client, NULL, 0) == 0);
        QueueError(&link, second.message_id);
        assert(gracht_client_await_multiple(client, both, 2, GRACHT_AWAIT_ALL) == 0);
        assert(gracht_client_get_status(client, &context) == GRACHT_MESSAGE_COMPLETED);
        assert(gracht_client_get_status(client, &second) == GRACHT_MESSAGE_ERROR);
        assert(gracht_client_abandon(client, &second) == 0);
        Gone(client, &second);
        assert(gracht_client_abandon(client, &context) == 0);
        Gone(client, &context);
        context = Request(client);
    } else if (!strcmp(argv[1], "await-sync-any")) {
        /* A synchronous ANY wait on three requests returns after the first
         * answer (here the second request's). The link has nothing more to
         * deliver, so a wait that kept pumping would end in the idle check.
         * The other two stay usable: one is awaited and read, one abandoned. */
        struct gracht_message_context second = Request(client);
        struct gracht_message_context third = Request(client);
        struct gracht_message_context* all[] = {&context, &second, &third};
        Queue(&link, second.message_id, SERVICE, ACTION, MESSAGE_FLAG_RESPONSE, 66);
        assert(gracht_client_await_multiple(client, all, 3, GRACHT_AWAIT_ANY) == 0);
        assert(link.Deliveries == 1 && link.Idle == 0);
        Pending(client, &context);
        Pending(client, &third);
        Result(client, &second, 66);
        assert(gracht_client_abandon(client, &third) == 0);
        Gone(client, &third);
        Queue(&link, context.message_id, SERVICE, ACTION, MESSAGE_FLAG_RESPONSE, 67);
        assert(gracht_client_await(client, &context, 0) == 0);
        Result(client, &context, 67);
        context = Request(client);
    } else if (!strcmp(argv[1], "await-sync-all")) {
        /* A synchronous ALL wait on two requests waits for both, which the link
         * delivers one per receive, the last request first. Each answer keeps
         * one of the two receive buffers until it is read, so two is the most
         * this pool can hold. */
        struct gracht_message_context second = Request(client);
        struct gracht_message_context* both[] = {&context, &second};
        Answers[0] = &context;
        AnswerCount = 1;
        link.Delivered = QueueNextAnswer;
        Queue(&link, second.message_id, SERVICE, ACTION, MESSAGE_FLAG_RESPONSE, second.message_id);
        assert(gracht_client_await_multiple(client, both, 2, GRACHT_AWAIT_ALL) == 0);
        assert(link.Deliveries == 2 && link.Idle == 0);
        Result(client, &second, second.message_id);
        Result(client, &context, context.message_id);
        context = Request(client);
    } else if (!strcmp(argv[1], "await-sync-error")) {
        /* The link fails during a synchronous wait. The wait ends with -1 and
         * the errno of the link instead of trying again. The answer that came
         * before the failure is kept, the other request stays pending, and both
         * can be awaited and read afterwards. ANY and ALL behave the same. */
        struct gracht_message_context second = Request(client);
        struct gracht_message_context* both[] = {&context, &second};
        link.IdleError = EPIPE;
        errno = 0;
        assert(gracht_client_await(client, &context, 0) == -1 && errno == EPIPE);
        assert(link.Idle == 1);
        Pending(client, &context);
        errno = 0;
        assert(gracht_client_await_multiple(client, both, 2, GRACHT_AWAIT_ANY) == -1);
        assert(errno == EPIPE && link.Idle == 2);
        Queue(&link, second.message_id, SERVICE, ACTION, MESSAGE_FLAG_RESPONSE, 77);
        errno = 0;
        assert(gracht_client_await_multiple(client, both, 2, GRACHT_AWAIT_ALL) == -1);
        assert(errno == EPIPE && link.Idle == 3 && link.Deliveries == 1);
        Pending(client, &context);
        assert(gracht_client_get_status(client, &second) == GRACHT_MESSAGE_COMPLETED);
        /* A dropped frame is different: a late answer for an unknown request
         * does not end the wait. */
        link.IdleError = 0;
        Answers[0] = &context;
        AnswerCount = 1;
        link.Delivered = QueueNextAnswer;
        Queue(&link, second.message_id + 100, SERVICE, ACTION, MESSAGE_FLAG_RESPONSE, 1);
        assert(gracht_client_await_multiple(client, both, 2, GRACHT_AWAIT_ALL) == 0);
        Result(client, &second, 77);
        Result(client, &context, context.message_id);
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
