/** Deferred messages keep their credentials; reused buffers start with none. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <gracht/server.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <utils.h>

static unsigned invocation;
static struct gracht_message* deferred;

static void
__CheckCredentials(
    const struct gracht_message* message)
{
    const struct gracht_security_context* credentials;

    credentials = gracht_server_get_security_context(message);
#if defined(MOLLENOS)
    assert(credentials && credentials->source == GRACHT_SECURITY_KERNEL);
    assert(credentials->principal == 123 && credentials->capabilities == 7);
#elif defined(_WIN32)
    assert(credentials == NULL);
#else
    assert(credentials && credentials->source == GRACHT_SECURITY_KERNEL);
    assert(credentials->pid == 123 && credentials->uid == 0 && credentials->gid == 7);
    assert(credentials->capture == GRACHT_SECURITY_CAPTURE_MESSAGE);
#endif
}

static gracht_conn_t
__Setup(struct gracht_link* link, gracht_handle_t set)
{
    (void)set;
    link->connection = 42;
    return 42;
}

static void
__Destroy(struct gracht_link* link, gracht_handle_t set)
{
    (void)link;
    (void)set;
}

static int
__Receive(struct gracht_link* link, struct gracht_message* message, unsigned flags)
{
    (void)link;
    (void)flags;
    // The buffer must not retain the previous sender's credentials.
    assert(gracht_server_get_security_context(message) == NULL);
#if defined(_WIN32) && !defined(MOLLENOS)
    assert(message->security.reserved == 0);
#endif
    if (invocation == 0) {
#if defined(MOLLENOS)
        message->security = (struct gracht_security_context){
            .source = GRACHT_SECURITY_KERNEL,
            .principal = 123,
            .capabilities = 7
        };
#elif defined(_WIN32)
        // Nonzero reserved bytes must not be treated as credentials.
        message->security = (struct gracht_security_context){.reserved = 123};
#else
        // Check that an authenticated POSIX link can report UID zero.
        message->security = (struct gracht_security_context){
            .source = GRACHT_SECURITY_KERNEL,
            .pid = 123,
            .capture = GRACHT_SECURITY_CAPTURE_MESSAGE,
            .uid = 0,
            .gid = 7
        };
#endif
    }
    message->client = 9;
    message->link = 42;
    message->size = GRACHT_MESSAGE_HEADER_SIZE;
    message->index = 0;
    memset(message->payload, 0, message->size);
    __gracht_write_u32(message->payload + MSG_INDEX_LEN, message->size);
    message->payload[MSG_INDEX_SID] = 61;
    message->payload[MSG_INDEX_AID] = 1;
    message->payload[MSG_INDEX_FLG] = MESSAGE_FLAG_ASYNC;
    return 0;
}

static void
__Invoke(struct gracht_message* message, struct gracht_buffer* buffer)
{
    (void)buffer;

    if (invocation == 0) {
        __CheckCredentials(message);
        deferred = malloc(GRACHT_MESSAGE_DEFERRABLE_SIZE(message));
        assert(deferred);
        gracht_server_defer_message(message, deferred);
    } else {
        assert(gracht_server_get_security_context(message) == NULL);
    }
    invocation++;
}

int
main(void)
{
    gracht_server_configuration_t configuration;
    gracht_server_t* server;
    struct gracht_link link = {.type = gracht_link_packet_based};
    gracht_protocol_function_t functions[] = {{1, (void*)__Invoke}};
    gracht_protocol_t protocol = GRACHT_PROTOCOL_INIT(61, "security", 1, functions);

    gracht_server_configuration_init(&configuration);
    gracht_server_configuration_set_num_workers(&configuration, 0);
    assert(gracht_server_create(&configuration, &server) == 0);
    link.ops.server.setup = __Setup;
    link.ops.server.destroy = __Destroy;
    link.ops.server.recv = __Receive;
    assert(gracht_server_add_link(server, &link) == 0);
    assert(gracht_server_register_protocol(server, &protocol) == 0);
    assert(gracht_server_handle_event(server, 42, 0) == 0);
    assert(gracht_server_handle_event(server, 42, 0) == 0);
    assert(invocation == 2);
    assert(gracht_server_get_security_context(NULL) == NULL);
    gracht_server_request_shutdown(server);
    assert(gracht_server_handle_event(server, 42, 0) == -1);
    __CheckCredentials(deferred);
    free(deferred);
    return 0;
}
