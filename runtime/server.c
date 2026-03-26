/**
 * Copyright 2019, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Gracht Server Type Definitions & Structures
 * - This header describes the base server-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <errno.h>
#include "aio.h"
#include "buffer_pool.h"
#include "stream_pool_registry.h"
#include "logging.h"
#include "gracht/server.h"
#include "thread_api.h"
#include "rwlock.h"
#include "utils.h"
#include "server_private.h"
#include "hashtable.h"
#include "stack.h"
#include "control.h"
#include <stdlib.h>
#include <string.h>

#define GRACHT_SERVER_MAX_LINKS 4

#define GRACHT_CLIENT_FLAG_STREAM  0x1
#define GRACHT_CLIENT_FLAG_CLEANUP 0x2

struct client_wrapper {
    gracht_conn_t                handle;
    struct gracht_link*          link;
    struct gracht_server_client* client;
};

struct broadcast_context {
    struct gracht_buffer* message;
    unsigned int          flags;
};

struct server_operations {
    void                   (*dispatch)(struct gracht_server*, struct gracht_message*);
    struct gracht_message* (*get_incoming_buffer)(struct gracht_server*, uint32_t streamMessageSize);
    void                   (*put_message)(struct gracht_server*, struct gracht_message*);
};

struct link_table {
    gracht_conn_t       handles[GRACHT_SERVER_MAX_LINKS];
    struct gracht_link* links[GRACHT_SERVER_MAX_LINKS];
};

enum server_state {
    SHUTDOWN,
    RUNNING,
    SHUTDOWN_REQUESTED
};

typedef struct gracht_server {
    enum server_state              state;
    struct server_operations*      ops;
    struct gracht_server_callbacks callbacks;
    struct gracht_worker_pool*     worker_pool;
    struct stack                   buffer_stack;
    size_t                         allocation_size;
    size_t                         stream_buffer_size;
    size_t                         stream_buffer_count;
    void*                          recv_buffer;
    struct gracht_buffer_pool*     recv_pool;
    struct gracht_stream_pool_registry stream_send_pools;
    struct gracht_stream_pool_registry stream_recv_pools;
    mtx_t                          stream_pools_lock;
    gracht_handle_t                set_handle;
    int                            set_handle_provided;
    gr_hashtable_t                 protocols;
    struct rwlock                  protocols_lock;
    gr_hashtable_t                 clients;
    struct rwlock                  clients_lock;
    struct link_table              link_table;
} gracht_server_t;

// api we export to generated files
GRACHTAPI int gracht_server_get_buffer(gracht_server_t*, gracht_buffer_t*);
GRACHTAPI int gracht_server_get_stream_buffer(gracht_server_t*, gracht_buffer_t*);
GRACHTAPI int gracht_server_get_stream_buffer_sized(gracht_server_t*, uint32_t, gracht_buffer_t*);
GRACHTAPI int gracht_server_respond(struct gracht_message*, gracht_buffer_t*);
GRACHTAPI int gracht_server_respond_stream(struct gracht_message*, gracht_buffer_t*);
GRACHTAPI int gracht_server_send_event(gracht_server_t*, gracht_conn_t client, gracht_buffer_t*, unsigned int flags);
GRACHTAPI int gracht_server_send_stream_event(gracht_server_t*, gracht_conn_t client, gracht_buffer_t*, unsigned int flags);
GRACHTAPI int gracht_server_broadcast_event(gracht_server_t*, gracht_buffer_t*, unsigned int flags);
GRACHTAPI int gracht_server_broadcast_stream_event(gracht_server_t*, gracht_buffer_t*, unsigned int flags);

static struct gracht_message* get_in_buffer_st(struct gracht_server*, uint32_t);
static void                   put_message_st(struct gracht_server*, struct gracht_message*);
static void                   dispatch_st(struct gracht_server*, struct gracht_message*);

static struct server_operations g_stOperations = {
    dispatch_st,
    get_in_buffer_st,
    put_message_st
};

static struct gracht_message* get_in_buffer_mt(struct gracht_server*, uint32_t);
static void                   put_message_mt(struct gracht_server*, struct gracht_message*);
static void                   dispatch_mt(struct gracht_server*, struct gracht_message*);

static struct server_operations g_mtOperations = {
    dispatch_mt,
    get_in_buffer_mt,
    put_message_mt
};

static void client_destroy(struct gracht_server*, gracht_conn_t);
static void client_subscribe(struct gracht_server_client*, uint8_t);
static void client_unsubscribe(struct gracht_server_client*, uint8_t);
static int  client_is_subscribed(struct gracht_server_client*, uint8_t);

static uint64_t client_hash(const void*);
static int      client_cmp(const void*, const void*);
static void     client_enum_destroy(int index, const void* element, void* userContext);
static void     client_enum_broadcast(int index, const void* element, void* userContext);
static int      server_protocol_uses_stream_pool(struct gracht_server*, uint8_t);


static int configure_server(struct gracht_server*, gracht_server_configuration_t*);

static int server_protocol_uses_stream_pool(struct gracht_server* server, uint8_t protocolId)
{
    struct gracht_protocol* protocol;
    int                     useStream = 0;

    rwlock_r_lock(&server->protocols_lock);
    protocol = gr_hashtable_get(&server->protocols, &(struct gracht_protocol){ .id = protocolId });
    if (protocol && (protocol->flags & GRACHT_PROTOCOL_FLAG_STREAM)) {
        useStream = 1;
    }
    rwlock_r_unlock(&server->protocols_lock);
    return useStream;
}

int gracht_server_create(gracht_server_configuration_t* config, gracht_server_t** serverOut)
{
    gracht_server_t* server;
    int              status;

    if (!config || !serverOut) {
        errno = EINVAL;
        return -1;
    }

    server = malloc(sizeof(gracht_server_t));
    if (!server) {
        errno = ENOMEM;
        return -1;
    }
    memset(server, 0, sizeof(gracht_server_t));
    server->set_handle = GRACHT_HANDLE_INVALID;
    mtx_init(&server->stream_pools_lock, mtx_plain);

    status = configure_server(server, config);
    if (status) {
        GRERROR(GRSTR("gracht_server_start: invalid configuration provided"));
        free(server);
        return -1;
    }

    // initialize static members of the instance
    rwlock_init(&server->protocols_lock);
    rwlock_init(&server->clients_lock);
    gr_hashtable_construct(&server->protocols, 0, sizeof(struct gracht_protocol), protocol_hash, protocol_cmp);
    gr_hashtable_construct(&server->clients, 0, sizeof(struct client_wrapper), client_hash, client_cmp);
    stack_construct(&server->buffer_stack, 8);

    // everything is set up - update state before registering control protocol
    server->state = RUNNING;

    gracht_server_register_protocol(server, &gracht_control_server_protocol);

    *serverOut = server;
    return 0;
}

static int configure_server(struct gracht_server* server, gracht_server_configuration_t* configuration)
{
    size_t bufferCount;
    int    status;

    // set the configuration params that are just transfer
    memcpy(&server->callbacks, &configuration->callbacks, sizeof(struct gracht_server_callbacks));

    // handle the aio descriptor
    if (configuration->set_descriptor_provided) {
        server->set_handle          = configuration->set_descriptor;
        server->set_handle_provided = 1;
    } else {
        server->set_handle = gracht_aio_create();
        if (server->set_handle == GRACHT_HANDLE_INVALID) {
            GRERROR(GRSTR("gracht_server: failed to create aio handle"));
            return -1;
        }
    }

    // configure the allocation size, we use the max message size and add
    // 512 bytes for context data
    server->allocation_size = configuration->max_message_size + 512;

    // handle the worker count, if the worker count is not provided we do not use
    // the dispatcher, but instead handle single-threaded.
    if (configuration->server_workers > 1) {
        status = gracht_worker_pool_create(server, configuration->server_workers, &server->worker_pool);
        if (status) {
            GRERROR(GRSTR("configure_server: failed to create the worker pool"));
            return -1;
        }
        server->ops = &g_mtOperations;
    } else {
        server->ops = &g_stOperations;
    }

    // handle the max message size override, otherwise we default to our default value.
    if (configuration->server_workers > 1) {
        bufferCount = (size_t)configuration->server_workers * 32;
        status      = gracht_buffer_pool_create(server->allocation_size, bufferCount, &server->recv_pool);
        if (status) {
            GRERROR(GRSTR("configure_server: failed to create the receive buffer pool"));
            return -1;
        }
    } else {
        server->recv_buffer = malloc(server->allocation_size);
        if (!server->recv_buffer) {
            GRERROR(GRSTR("configure_server: failed to allocate memory for incoming messages"));
            return -1;
        }
    }

    server->stream_buffer_size = (size_t)(configuration->stream_buffer_size > 0 ?
            configuration->stream_buffer_size : configuration->max_message_size);
    if (!server->stream_buffer_size) {
        server->stream_buffer_size = GRACHT_DEFAULT_MESSAGE_SIZE;
    }
    server->stream_buffer_count = (size_t)(configuration->stream_buffer_count > 0 ? configuration->stream_buffer_count : 8);
    return 0;
}

int gracht_server_add_link(gracht_server_t* server, struct gracht_link* link)
{
    gracht_conn_t connection;
    int           tableIndex;

    if (!server || !link) {
        errno = EINVAL;
        return -1;
    }

    if (server->state != RUNNING) {
        errno = EPERM;
        return -1;
    }

    for (tableIndex = 0; tableIndex < GRACHT_SERVER_MAX_LINKS; tableIndex++) {
        if (!server->link_table.links[tableIndex]) {
            break;
        }
    }

    if (tableIndex >= GRACHT_SERVER_MAX_LINKS) {
        GRERROR(GRSTR("gracht_server_add_link: the maximum link count was reached"));
        errno = ENOENT;
        return -1;
    }

    connection = link->ops.server.setup(link, server->set_handle);
    if (connection == GRACHT_CONN_INVALID) {
        GRERROR(GRSTR("gracht_server_add_link: provided link failed setup"));
        return -1;
    }

    server->link_table.handles[tableIndex] = connection;
    server->link_table.links[tableIndex]   = link;
    return 0;
}

static int handle_connection(struct gracht_server* server, struct gracht_link* link)
{
    struct gracht_server_client* client;

    int status = link->ops.server.accept_client(link, server->set_handle, &client);
    if (status) {
        GRERROR(GRSTR("gracht_server: failed to accept client"));
        return status;
    }

    // this is a streaming client, which means we handle them differently if they should
    // unsubscribe to certain protocols. Streaming clients are subscribed to all from start
    client->flags |= GRACHT_CLIENT_FLAG_STREAM;
    memset(&client->subscriptions[0], 0xFF, sizeof(client->subscriptions));
    
    rwlock_w_lock(&server->clients_lock);
    gr_hashtable_set(&server->clients, &(struct client_wrapper) { 
        .handle = client->handle,
        .link = link,
        .client = client
    });
    rwlock_w_unlock(&server->clients_lock);

    // invoke the new client callback at last
    if (server->callbacks.clientConnected) {
        server->callbacks.clientConnected(client->handle);
    }
    return 0;
}

static struct gracht_message* get_in_buffer_st(struct gracht_server* server, uint32_t streamMessageSize)
{
    struct gracht_message*      message;
    struct gracht_buffer_pool*  pool;
    size_t                      requestedSize;

    if (streamMessageSize == 0) {
        message = (struct gracht_message*)server->recv_buffer;
        message->server = server;
        message->index  = server->allocation_size;
        return message;
    }

    requestedSize = gracht_stream_normalize_buffer_size((size_t)streamMessageSize + 512, server->stream_buffer_size + 512);
    mtx_lock(&server->stream_pools_lock);
    pool = gracht_stream_pool_registry_get_or_create(&server->stream_recv_pools, requestedSize, server->stream_buffer_count);
    message = pool ? gracht_buffer_pool_acquire(pool) : NULL;
    mtx_unlock(&server->stream_pools_lock);
    if (!message) {
        return NULL;
    }
    message->server = server;
    message->index  = (uint32_t)requestedSize;
    return message;
}

static void put_message_st(struct gracht_server* server, struct gracht_message* message)
{
    if (!message || message == server->recv_buffer) {
        return;
    }

    mtx_lock(&server->stream_pools_lock);
    gracht_stream_pool_registry_release(&server->stream_recv_pools, message);
    mtx_unlock(&server->stream_pools_lock);
}

static void dispatch_st(struct gracht_server* server, struct gracht_message* message)
{
    server_invoke_action(server, message);
}

static void dispatch_mt(struct gracht_server* server, struct gracht_message* message)
{
    uint8_t protocol = *((uint8_t*)&message->payload[message->index + MSG_INDEX_SID]);

    // due to the fact that the control protocol modifies state on the server, especially
    // client state - we want to ensure that these methods are run on orchestrator thread.
    if (protocol == 0) {
        server_invoke_action(server, message);
        server_cleanup_message(server, message);
    }
    else {
        gracht_worker_pool_dispatch(server->worker_pool, message);
    }
}

static struct gracht_message* get_in_buffer_mt(struct gracht_server* server, uint32_t streamMessageSize)
{
    struct gracht_message* message;

    if (streamMessageSize == 0) {
        message = gracht_buffer_pool_acquire(server->recv_pool);
        if (!message) {
            return NULL;
        }
        message->server = server;
        message->index  = server->allocation_size;
        return message;
    }

    mtx_lock(&server->stream_pools_lock);
    {
        struct gracht_buffer_pool* pool;
        size_t requestedSize = gracht_stream_normalize_buffer_size((size_t)streamMessageSize + 512, server->stream_buffer_size + 512);

        pool = gracht_stream_pool_registry_get_or_create(&server->stream_recv_pools, requestedSize, server->stream_buffer_count);
        message = pool ? gracht_buffer_pool_acquire(pool) : NULL;
        if (message) {
            message->index = (uint32_t)requestedSize;
        }
    }
    mtx_unlock(&server->stream_pools_lock);
    if (!message) {
        return NULL;
    }
    message->server = server;
    return message;
}

static void put_message_mt(struct gracht_server* server, struct gracht_message* message)
{
    mtx_lock(&server->stream_pools_lock);
    if (!gracht_stream_pool_registry_release(&server->stream_recv_pools, message)) {
        gracht_buffer_pool_release(server->recv_pool, message);
    }
    mtx_unlock(&server->stream_pools_lock);
}

static int handle_packet(struct gracht_server* server, struct gracht_link* link)
{
    struct gracht_message* message;
    int                    status;
    uint32_t               incomingLength = 0;
    uint8_t                protocolId = 0;
    uint32_t               streamMessageSize = 0;
    GRTRACE(GRSTR("handle_packet(conn=%i)"), link->connection);

    if (link->ops.server.peek) {
        status = link->ops.server.peek(link, &incomingLength, &protocolId, GRACHT_MESSAGE_BLOCK);
        if (status) {
            if (errno != ENODATA) {
                GRERROR(GRSTR("handle_packet link->ops.server.peek returned %i"), errno);
            }
            return status;
        }

        if (server_protocol_uses_stream_pool(server, protocolId)) {
            streamMessageSize = incomingLength;
        } else if (incomingLength > (uint32_t)(server->allocation_size - 512)) {
            errno = EMSGSIZE;
            return -1;
        }
    }

    message = server->ops->get_incoming_buffer(server, streamMessageSize);
    if (!message) {
        GRERROR(GRSTR("handle_packet ran out of receiving buffers"));
        errno = ENOMEM;
        return -1;
    }

    status = link->ops.server.recv(link, message, GRACHT_MESSAGE_BLOCK);
    if (status) {
        if (errno != ENODATA) {
            GRERROR(GRSTR("handle_packet link->ops.server.recv returned %i"), errno);
        }
        server->ops->put_message(server, message);
        return status;
    }

    server->ops->dispatch(server, message);
    return 0;
}

static struct gracht_link* get_link_by_conn(struct gracht_server* server, gracht_conn_t connection)
{
    for (int i = 0; i < GRACHT_SERVER_MAX_LINKS; i++) {
        if (server->link_table.handles[i] == connection) {
            return server->link_table.links[i];
        }
    }
    return NULL;
}

static int handle_client_event(struct gracht_server* server, gracht_conn_t handle, uint32_t events)
{
    int status;
    GRTRACE(GRSTR("handle_client_event %" F_CONN_T ", 0x%x"), handle, events);

    // Check for control event. On non-passive sockets, control event is the
    // disconnect event.
    if (events & GRACHT_AIO_EVENT_DISCONNECT) {
        client_destroy(server, handle);
    } else if ((events & GRACHT_AIO_EVENT_IN) || !events) {
        struct client_wrapper* entry;

        rwlock_r_lock(&server->clients_lock);
        entry = gr_hashtable_get(&server->clients, &(struct client_wrapper){ .handle = handle });
        while (entry) {
            uint32_t               incomingLength = 0;
            uint8_t                protocolId = 0;
            uint32_t               streamMessageSize = 0;
            struct gracht_message* message;

            if (entry->link->ops.server.peek_client) {
                status = entry->link->ops.server.peek_client(entry->client, &incomingLength, &protocolId, 0);
                if (status) {
                    rwlock_r_unlock(&server->clients_lock);

                    if (errno != ENODATA && errno != EAGAIN && errno != EFAULT) {
                        GRERROR(GRSTR("handle_client_event server_object.link->peek_client returned %i"), errno);
                    }

                    if (errno == EFAULT) {
                        GRTRACE(GRSTR("handle_client_event client disconnected, cleaning up"));
                        client_destroy(server, handle);
                    }
                    return 0;
                }

                if (server_protocol_uses_stream_pool(server, protocolId)) {
                    streamMessageSize = incomingLength;
                } else if (incomingLength > (uint32_t)(server->allocation_size - 512)) {
                    rwlock_r_unlock(&server->clients_lock);
                    errno = EMSGSIZE;
                    return -1;
                }
            }

            message = server->ops->get_incoming_buffer(server, streamMessageSize);
            if (!message) {
                rwlock_r_unlock(&server->clients_lock);
                GRERROR(GRSTR("handle_client_event ran out of receiving buffers"));
                errno = ENOMEM;
                return -1;
            }
            
            status = entry->link->ops.server.recv_client(entry->client, message, 0);
            if (status) {
                server->ops->put_message(server, message);
                rwlock_r_unlock(&server->clients_lock);

                // silence the three below error codes, those are expected
                if (errno != ENODATA && errno != EAGAIN && errno != EFAULT) {
                    GRERROR(GRSTR("handle_client_event server_object.link->recv_client returned %i"), errno);
                }

                // detect cases of disconnection or transmission failures that are fatal.
                // in these cases we expect the underlying link to specify EFAULT
                if (errno == EFAULT) {
                    GRTRACE(GRSTR("handle_client_event client disconnected, cleaning up"));
                    client_destroy(server, handle);
                }
                return 0;
            }

            server->ops->dispatch(server, message);
        }
        rwlock_r_unlock(&server->clients_lock);
    }
    return 0;
}

static int gracht_server_shutdown(gracht_server_t* server)
{
    void* buffer;
    int   i;
    GRTRACE(GRSTR("gracht_server_shutdown()"));

    if (server->state == SHUTDOWN) {
        errno = EALREADY;
        return -1;
    }
    server->state = SHUTDOWN;
    
    // destroy all our workers
    if (server->worker_pool) {
        gracht_worker_pool_destroy(server->worker_pool);
    }

    // start out by destroying all our clients
    rwlock_w_lock(&server->clients_lock);
    gr_hashtable_enumerate(&server->clients, client_enum_destroy, server);
    rwlock_w_unlock(&server->clients_lock);

    // destroy all our links
    for (i = 0; i < GRACHT_SERVER_MAX_LINKS; i++) {
        if (server->link_table.links[i]) {
            server->link_table.links[i]->ops.server.destroy(server->link_table.links[i], server->set_handle);
            server->link_table.links[i] = NULL;
        }
    }
    
    // destroy the event descriptor
    if (server->set_handle != GRACHT_HANDLE_INVALID && !server->set_handle_provided) {
        gracht_aio_destroy(server->set_handle);
    }

    // iterate all our serializer buffers and destroy them
    buffer = stack_pop(&server->buffer_stack);
    while (buffer) {
        free(buffer);
        buffer = stack_pop(&server->buffer_stack);
    }

    // destroy all our allocated resources
    if (server->recv_pool) {
        gracht_buffer_pool_destroy(server->recv_pool);
    }

    gracht_stream_pool_registry_destroy(&server->stream_send_pools);
    gracht_stream_pool_registry_destroy(&server->stream_recv_pools);
    
    if (server->recv_buffer) {
        free(server->recv_buffer);
    }

    stack_destroy(&server->buffer_stack);
    gr_hashtable_destroy(&server->protocols);
    gr_hashtable_destroy(&server->clients);
    mtx_destroy(&server->stream_pools_lock);
    rwlock_destroy(&server->protocols_lock);
    rwlock_destroy(&server->clients_lock);
    free(server);
    return 0;
}

void gracht_server_request_shutdown(gracht_server_t* server)
{
    if (!server) {
        errno = EINVAL;
        return;
    }

    if (server->state != RUNNING) {
        errno = EPERM;
        return;
    }
    
    server->state = SHUTDOWN_REQUESTED;
}

void server_invoke_action(struct gracht_server* server, struct gracht_message* recvMessage)
{
    gracht_protocol_function_t* function;
    gracht_buffer_t             buffer = { .data = (char*)&recvMessage->payload[0], .index = recvMessage->index };
    uint32_t                    messageId;
    uint8_t                     protocol;
    uint8_t                     action;

    messageId = GB_MSG_ID(&buffer);
    protocol  = GB_MSG_SID(&buffer);
    action    = GB_MSG_AID(&buffer);
    GRTRACE(GRSTR("server_invoke_action %u: %u/%u"), messageId, protocol, action);

    rwlock_r_lock(&server->protocols_lock);
    function = get_protocol_action(&server->protocols, protocol, action);
    rwlock_r_unlock(&server->protocols_lock);
    if (!function) {
        GRWARNING(GRSTR("server_invoke_action failed to invoke server action"));
        gracht_control_event_error_single(server, recvMessage->client, messageId, ENOENT);
        return;
    }

    // skip the message header when invoking
    buffer.index += GRACHT_MESSAGE_HEADER_SIZE;
    ((server_invoke_t)function->address)(recvMessage, &buffer);
}

void server_cleanup_message(struct gracht_server* server, struct gracht_message* recvMessage)
{
    if (!server || !recvMessage) {
        return;
    }

    mtx_lock(&server->stream_pools_lock);
    if (!gracht_stream_pool_registry_release(&server->stream_recv_pools, recvMessage) && server->recv_pool) {
        gracht_buffer_pool_release(server->recv_pool, recvMessage);
    }
    mtx_unlock(&server->stream_pools_lock);
}

int gracht_server_handle_event(gracht_server_t* server, gracht_conn_t handle, unsigned int events)
{
    struct gracht_link* link;

    if (!server) {
        errno = EINVAL;
        return -1;
    }

    // assert current state, and cleanup if state is request shutdown
    if (server->state != RUNNING) {
        if (server->state == SHUTDOWN_REQUESTED) {
            gracht_server_shutdown(server);
        }
        errno = EPIPE;
        return -1;
    }

    link = get_link_by_conn(server, handle);
    if (!link) {
        return handle_client_event(server, handle, events);
    }

    if (link->type == gracht_link_stream_based) {
        return handle_connection(server, link);
    }
    else if (link->type == gracht_link_packet_based) {
        return handle_packet(server, link);
    }
    return -1;
}

int gracht_server_main_loop(gracht_server_t* server)
{
    gracht_aio_event_t events[32];
    int                i;

    if (!server) {
        errno = EINVAL;
        return -1;
    }

    if (server->state != RUNNING) {
        errno = EPERM;
        return -1;
    }

    GRTRACE(GRSTR("gracht_server: started..."));
    while (server->state == RUNNING) {
        GRTRACE(GRSTR("gracht_server: waiting for events..."));
        int num_events = gracht_io_wait(server->set_handle, &events[0], 32);
        GRTRACE(GRSTR("gracht_server: %i events received!"), num_events);
        for (i = 0; i < num_events; i++) {
            gracht_conn_t handle = gracht_aio_event_handle(&events[i]);
            uint32_t      flags  = gracht_aio_event_events(&events[i]);

            GRTRACE(GRSTR("gracht_server: event %u from %" F_CONN_T), flags, handle);
            if (gracht_server_handle_event(server, handle, flags) == -1 && errno == EPIPE) {
                // server has been shutdown by the handle_event
                return 0;
            }
        }
    }

    return gracht_server_shutdown(server);
}

int gracht_server_get_buffer(gracht_server_t* server, gracht_buffer_t* buffer)
{
    void* data;
    if (!server) {
        errno = EINVAL;
        return -1;
    }

    data = stack_pop(&server->buffer_stack);
    if (!data) {
        data = malloc(server->allocation_size);
        if (!data) {
            errno = ENOMEM;
            return -1;
        }
    }

    // this should always return a safe buffer to use for the request
    buffer->data  = data;
    buffer->index = 0;
    return 0;
}

int gracht_server_get_stream_buffer(gracht_server_t* server, gracht_buffer_t* buffer)
{
    return gracht_server_get_stream_buffer_sized(server, 0, buffer);
}

int gracht_server_get_stream_buffer_sized(gracht_server_t* server, uint32_t requiredSize, gracht_buffer_t* buffer)
{
    struct gracht_buffer_pool* pool;
    size_t                     normalizedSize;

    if (!server || !buffer) {
        errno = EINVAL;
        return -1;
    }

    normalizedSize = gracht_stream_normalize_buffer_size(requiredSize, server->stream_buffer_size);
    mtx_lock(&server->stream_pools_lock);
    pool = gracht_stream_pool_registry_get_or_create(&server->stream_send_pools, normalizedSize, server->stream_buffer_count);
    if (pool) {
        buffer->data = gracht_buffer_pool_acquire(pool);
    } else {
        buffer->data = NULL;
    }
    mtx_unlock(&server->stream_pools_lock);
    if (!buffer->data) {
        errno = ENOMEM;
        return -1;
    }

    buffer->index = 0;
    return 0;
}

static void __release_send_buffer(gracht_server_t* server, void* data, int stream)
{
    if (stream) {
        mtx_lock(&server->stream_pools_lock);
        gracht_stream_pool_registry_release(&server->stream_send_pools, data);
        mtx_unlock(&server->stream_pools_lock);
    } else {
        stack_push(&server->buffer_stack, data);
    }
}

static int __server_respond(struct gracht_message* messageContext, gracht_buffer_t* message, int stream)
{
    struct client_wrapper* entry;
    int                    status;
    GRTRACE(GRSTR("gracht_server_respond()"));

    if (!messageContext || !message) {
        GRERROR(GRSTR("gracht_server: null message or context"));
        errno = EINVAL;
        return -1;
    }

    // update message header
    GB_MSG_ID_0(message)  = *((uint32_t*)&messageContext->payload[messageContext->index]);
    GB_MSG_LEN_0(message) = message->index;

    rwlock_r_lock(&messageContext->server->clients_lock);
    entry = gr_hashtable_get(&messageContext->server->clients, &(struct client_wrapper){ .handle = messageContext->client });
    if (!entry) {
        struct gracht_link* link;

        rwlock_r_unlock(&messageContext->server->clients_lock);
        link = get_link_by_conn(messageContext->server, messageContext->link);
        if (!link) {
            errno = ENODEV;
            if (stream) {
                __release_send_buffer(messageContext->server, message->data, stream);
            }
            return -1;
        }
        status = link->ops.server.send(link, messageContext, message);
    } else {
        status = entry->link->ops.server.send_client(entry->client, message, GRACHT_MESSAGE_BLOCK);
        rwlock_r_unlock(&messageContext->server->clients_lock);
    }

    __release_send_buffer(messageContext->server, message->data, stream);
    return status;
}

int gracht_server_respond(struct gracht_message* messageContext, gracht_buffer_t* message)
{
    return __server_respond(messageContext, message, 0);
}

int gracht_server_respond_stream(struct gracht_message* messageContext, gracht_buffer_t* message)
{
    return __server_respond(messageContext, message, 1);
}

static int __server_send_event(gracht_server_t* server, gracht_conn_t client, gracht_buffer_t* message, unsigned int flags, int stream)
{
    struct client_wrapper* clientEntry;
    int                    status;
    GRTRACE(GRSTR("gracht_server_send_event()"));

    if (!server || !message) {
        errno = EINVAL;
        return -1;
    }

    // update message header
    GB_MSG_LEN_0(message) = message->index;

    rwlock_r_lock(&server->clients_lock);
    clientEntry = gr_hashtable_get(&server->clients, &(struct client_wrapper){ .handle = client });
    if (!clientEntry) {
        rwlock_r_unlock(&server->clients_lock);
        errno = ENOENT;
        if (stream) {
            __release_send_buffer(server, message->data, stream);
        }
        return -1;
    }

    // When sending target specific events - we do not care about subscriptions
    status = clientEntry->link->ops.server.send_client(clientEntry->client, message, flags);
    rwlock_r_unlock(&server->clients_lock);

    __release_send_buffer(server, message->data, stream);
    return status;
}

int gracht_server_send_event(gracht_server_t* server, gracht_conn_t client, gracht_buffer_t* message, unsigned int flags)
{
    return __server_send_event(server, client, message, flags, 0);
}

int gracht_server_send_stream_event(gracht_server_t* server, gracht_conn_t client, gracht_buffer_t* message, unsigned int flags)
{
    return __server_send_event(server, client, message, flags, 1);
}

static int __server_broadcast_event(gracht_server_t* server, gracht_buffer_t* message, unsigned int flags, int stream)
{
    struct broadcast_context context = {
        .message = message,
        .flags   = flags
    };

    if (!server || !message) {
        errno = EINVAL;
        return -1;
    }

    // update message header
    GB_MSG_LEN_0(message) = message->index;

    rwlock_r_lock(&server->clients_lock);
    gr_hashtable_enumerate(&server->clients, client_enum_broadcast, &context);
    rwlock_r_unlock(&server->clients_lock);

    __release_send_buffer(server, message->data, stream);
    return 0;
}

int gracht_server_broadcast_event(gracht_server_t* server, gracht_buffer_t* message, unsigned int flags)
{
    return __server_broadcast_event(server, message, flags, 0);
}

int gracht_server_broadcast_stream_event(gracht_server_t* server, gracht_buffer_t* message, unsigned int flags)
{
    return __server_broadcast_event(server, message, flags, 1);
}

int gracht_server_register_protocol(gracht_server_t* server, gracht_protocol_t* protocol)
{
    if (!server || !protocol) {
        errno = EINVAL;
        return -1;
    }
    
    if (server->state != RUNNING) {
        errno = EPERM;
        return -1;
    }

    rwlock_w_lock(&server->protocols_lock);
    if (gr_hashtable_get(&server->protocols, protocol)) {
        rwlock_w_unlock(&server->protocols_lock);
        errno = EEXIST;
        return -1;
    }
    gr_hashtable_set(&server->protocols, protocol);
    rwlock_w_unlock(&server->protocols_lock);
    return 0;
}

void gracht_server_unregister_protocol(gracht_server_t* server, gracht_protocol_t* protocol)
{
    if (!server || !protocol) {
        errno = EINVAL;
        return;
    }

    if (server->state != RUNNING) {
        errno = EPERM;
        return;
    }
    
    rwlock_w_lock(&server->protocols_lock);
    gr_hashtable_remove(&server->protocols, protocol);
    rwlock_w_unlock(&server->protocols_lock);
}

gracht_handle_t gracht_server_get_aio_handle(gracht_server_t* server)
{
    if (!server) {
        errno = EINVAL;
        return GRACHT_HANDLE_INVALID;
    }

    if (server->state != RUNNING) {
        errno = EPERM;
        return GRACHT_HANDLE_INVALID;
    }

    return server->set_handle;
}

void gracht_server_defer_message(struct gracht_message* in, struct gracht_message* out)
{
    if (!in || !out) {
        return;
    }

    memcpy(out, in, GRACHT_MESSAGE_DEFERRABLE_SIZE(in));
}

// Client helpers
static void client_destroy(struct gracht_server* server, gracht_conn_t client)
{
    struct client_wrapper* entry;

    if (server->callbacks.clientDisconnected) {
        server->callbacks.clientDisconnected(client);
    }

    rwlock_w_lock(&server->clients_lock);
    entry = gr_hashtable_remove(&server->clients, &(struct client_wrapper){ .handle = client });
    if (entry) {
        entry->link->ops.server.destroy_client(entry->client, server->set_handle);
    }
    rwlock_w_unlock(&server->clients_lock);
}

// Client subscription helpers
static void client_subscribe(struct gracht_server_client* client, uint8_t id)
{
    int block  = id / 32;
    int offset = id % 32;

    if (id == 0xFF) {
        // subscribe to all
        memset(&client->subscriptions[0], 0xFF, sizeof(client->subscriptions));
        return;
    }

    client->subscriptions[block] |= (1 << offset);
}

static void client_unsubscribe(struct gracht_server_client* client, uint8_t id)
{
    int block  = id / 32;
    int offset = id % 32;

    if (id == 0xFF) {
        // unsubscribe to all
        memset(&client->subscriptions[0], 0, sizeof(client->subscriptions));
        return;
    }

    client->subscriptions[block] &= ~(1 << offset);
}

static int client_is_subscribed(struct gracht_server_client* client, uint8_t id)
{
    int block  = id / 32;
    int offset = id % 32;
    return (client->subscriptions[block] & (1 << offset)) != 0;
}

// Server control protocol implementation
void gracht_control_subscribe_invocation(const struct gracht_message* message, const uint8_t protocol)
{
    struct client_wrapper* entry;
    struct client_wrapper  newEntry;
    GRTRACE(GRSTR("gracht_control_subscribe_invocation(protocol=%u, client=%i)"), protocol, message->client);
    
    // When dealing with connectionless clients, they aren't really created in the client register. To deal
    // with this, we actually create a record for them, so we can support connection-less events. This means
    // that connection-less clients aren't considered connected unless they subscribe to some protocol - even
    // if they actually use the functions provided by the protocol. It is also possible to receive targetted
    // events that come in response to a function call even without subscribing.
    rwlock_r_lock(&message->server->clients_lock);
    entry = gr_hashtable_get(&message->server->clients, &(struct client_wrapper){ .handle = message->client });
    if (!entry) {
        // So, client did not have a record, at this point we then know this message was received on a 
        // connection-less stream, meaning we do not currently hold another _read_lock on this thread, thus we can
        // release our reader lock and acqurie the write-lock
        rwlock_r_unlock(&message->server->clients_lock);

        // lookup the connection as the client wasn't recorded on a specific link
        newEntry.link = get_link_by_conn(message->server, message->link);
        if (newEntry.link->ops.server.create_client(newEntry.link, (struct gracht_message*)message, &newEntry.client)) {
            GRERROR(GRSTR("gracht_control_subscribe_invocation server_object.link->create_client returned error"));
            return;
        }

        newEntry.handle = message->client;

        // this does not have to be serialized with the above read lock due to the fact that all
        // write-locks are only acquired by this thread. So any changes made are only the ones we make
        // right now
        rwlock_w_lock(&message->server->clients_lock);
        gr_hashtable_set(&message->server->clients, &newEntry);
        rwlock_w_unlock(&message->server->clients_lock);

        if (message->server->callbacks.clientConnected) {
            message->server->callbacks.clientConnected(message->client);
        }

        // not really neccessary but for correctness
        rwlock_r_lock(&message->server->clients_lock);
        
        // set the entry pointer
        entry = &newEntry;
    } else {
        // make sure if they were marked cleanup that we remove that
        entry->client->flags &= ~(GRACHT_CLIENT_FLAG_CLEANUP);
    }

    client_subscribe(entry->client, protocol);
    rwlock_r_unlock(&message->server->clients_lock);
}

void gracht_control_unsubscribe_invocation(const struct gracht_message* message, const uint8_t protocol)
{
    struct client_wrapper* entry;
    int                    cleanup = 0;
    
    rwlock_r_lock(&message->server->clients_lock);
    entry = gr_hashtable_get(&message->server->clients, &(struct client_wrapper){ .handle = message->client });
    if (!entry) {
        rwlock_r_unlock(&message->server->clients_lock);
        return;
    }

    client_unsubscribe(entry->client, protocol);
    
    // cleanup the client if we unsubscribe, but do not do it from here as the client
    // structure will be reffered later on
    if (protocol == 0xFF) {
        if (!(entry->client->flags & GRACHT_CLIENT_FLAG_STREAM)) {
            entry->client->flags |= GRACHT_CLIENT_FLAG_CLEANUP; // this flag is not needed
            cleanup = 1;
        }
    }
    rwlock_r_unlock(&message->server->clients_lock);

    // when receiving unsubscribe events on connection-less links we must check
    // after handling messages whether a client has been marked for cleanup
    // in this case we do not hold the lock and can therefore actually take the write-lock
    if (cleanup) {
        client_destroy(message->server, message->client);
    }
}

static uint64_t client_hash(const void* element)
{
    const struct client_wrapper* client = element;
    return (uint64_t)client->handle;
}

static int client_cmp(const void* element1, const void* element2)
{
    const struct client_wrapper* client1 = element1;
    const struct client_wrapper* client2 = element2;
    return client1->handle == client2->handle ? 0 : 1;
}

static void client_enum_broadcast(int index, const void* element, void* userContext)
{
    const struct client_wrapper* entry   = element;
    struct broadcast_context*    context = userContext;
    uint8_t                      protocol = GB_MSG_SID_0(context->message);
    GRTRACE(GRSTR("client_enum_broadcast()"));
    (void)index;

    if (client_is_subscribed(entry->client, protocol)) {
        entry->link->ops.server.send_client(entry->client, context->message, context->flags);
    }
}

static void client_enum_destroy(int index, const void* element, void* userContext)
{
    const struct client_wrapper* entry  = element;
    struct gracht_server*        server = userContext;
    (void)index;
    (void)userContext;
    entry->link->ops.server.destroy_client(entry->client, server->set_handle);
}
