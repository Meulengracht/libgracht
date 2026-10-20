/* Force response delivery at the registration/wait boundary without sleeps.
 * Only the thread primitives are intercepted; the client and tables are real. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "thread_api.h"
static int native_unlock(mtx_t* mutex) { return mtx_unlock(mutex); }
static int native_wait(cnd_t* condition, mtx_t* mutex) { return cnd_wait(condition, mutex); }
static int test_unlock(mtx_t*);
static int test_wait(cnd_t*, mtx_t*);
#undef mtx_unlock
#undef cnd_wait
#define mtx_unlock test_unlock
#define cnd_wait test_wait
#include "../../runtime/client.c"
#undef mtx_unlock
#undef cnd_wait
#define mtx_unlock native_unlock
#define cnd_wait native_wait

static gracht_client_t* test_client;
static int early, waits, responses, spurious, duplicate, concurrent, start;
static mtx_t gate;
static cnd_t started;

static int responder(void* context)
{
    mtx_lock(&gate);
    while (!start) cnd_wait(&started, &gate);
    mtx_unlock(&gate);
    gracht_control_error_invocation(test_client, (uint32_t)(uintptr_t)context, EIO);
    return 0;
}

static void complete(void)
{
    gracht_control_error_invocation(test_client, ++responses, EIO);
    if (duplicate) gracht_control_error_invocation(test_client, responses, EIO);
}

static int test_unlock(mtx_t* mutex)
{
    int result = mtx_unlock(mutex);
    if (early && mutex == &test_client->messages_lock) {
        early = 0;
        complete();
    }
    if (concurrent && mutex == &test_client->messages_lock) {
        mtx_lock(&gate);
        start = 1;
        for (int i = 0; i < 4; i++) cnd_signal(&started);
        mtx_unlock(&gate);
    }
    return result;
}

static int test_wait(cnd_t* condition, mtx_t* mutex)
{
    struct gracht_message_awaiter* awaiter =
        (void*)((char*)condition - offsetof(struct gracht_message_awaiter, event));
    /* Waiting with a satisfied predicate loses an already-delivered signal. */
    assert(awaiter->current_count < awaiter->count);
    assert((awaiter->flags & GRACHT_AWAIT_ALL) || !awaiter->current_count);
    waits++;
    if (concurrent) return cnd_wait(condition, mutex);
    mtx_unlock(mutex);
    if (spurious) spurious = 0;
    else complete();
    mtx_lock(mutex);
    return thrd_success;
}

int main(int argc, char** argv)
{
    gracht_client_t client = {0};
    struct gracht_message_context messages[4] = {{1}, {2}, {3}, {4}};
    struct gracht_message_context* contexts[] = {&messages[0], &messages[1], &messages[2], &messages[3]};
    thrd_t workers[4];
    unsigned flags = GRACHT_AWAIT_ASYNC;
    int count = 1;
    assert(argc == 2);
    test_client = &client;
    client.current_awaiter_id = 1;
    mtx_init(&client.messages_lock, mtx_plain);
    mtx_init(&client.awaiters_lock, mtx_plain);
    gr_hashtable_construct(&client.messages, 16, sizeof(struct gracht_message_descriptor), message_hash, message_cmp);
    gr_hashtable_construct(&client.awaiters, 16, sizeof(struct gracht_message_awaiter_entry), awaiter_hash, awaiter_cmp);
    for (int id = 1; id <= 4; id++) {
        struct gracht_message_descriptor entry = {.id = id, .status = GRACHT_MESSAGE_INPROGRESS};
        gr_hashtable_set(&client.messages, &entry);
    }
    if (!strcmp(argv[1], "early")) early = 1;
    else if (!strcmp(argv[1], "spurious")) spurious = 1;
    else if (!strcmp(argv[1], "all")) { flags |= GRACHT_AWAIT_ALL; count = 2; }
    else if (!strcmp(argv[1], "duplicate")) { flags |= GRACHT_AWAIT_ALL; count = 2; duplicate = 1; }
    else if (!strcmp(argv[1], "concurrent-all") || !strcmp(argv[1], "concurrent-any")) {
        concurrent = 1; count = 4;
        if (!strcmp(argv[1], "concurrent-all")) flags |= GRACHT_AWAIT_ALL;
        mtx_init(&gate, mtx_plain); cnd_init(&started);
        for (int i = 0; i < 4; i++) assert(!thrd_create(&workers[i], responder, (void*)(uintptr_t)(i + 1)));
    }
    else if (!strcmp(argv[1], "any")) count = 2;
    else assert(!strcmp(argv[1], "normal"));
    assert(!gracht_client_await_multiple(&client, contexts, count, flags));
    if (concurrent) {
        for (int i = 0; i < 4; i++) { int status; assert(!thrd_join(workers[i], &status)); assert(!status); }
        cnd_destroy(&started); mtx_destroy(&gate);
    } else {
        assert(responses == ((flags & GRACHT_AWAIT_ALL) ? 2 : 1));
        assert(waits == (!strcmp(argv[1], "early") ? 0 :
                         !strcmp(argv[1], "spurious") || (flags & GRACHT_AWAIT_ALL) ? 2 : 1));
    }
    /* A response after ANY has removed the awaiter must not touch freed memory. */
    gracht_control_error_invocation(&client, 2, EIO);
    gr_hashtable_destroy(&client.awaiters);
    gr_hashtable_destroy(&client.messages);
    mtx_destroy(&client.awaiters_lock);
    mtx_destroy(&client.messages_lock);
    puts("RPC await ordering regression passed");
    return 0;
}
