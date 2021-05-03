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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Gracht Server Type Definitions & Structures
 * - This header describes the base server-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <errno.h>
#include "aio.h"
#include "arena.h"
#include "debug.h"
#include "gracht/server.h"
#include "gracht/link/link.h"
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
    struct gracht_message* (*get_incoming_buffer)(struct gracht_server*);
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
    struct stack                   bufferStack;
    size_t                         allocationSize;
    void*                          recvBuffer;
    gracht_handle_t                set_handle;
    int                            set_handle_provided;
    struct gracht_arena*           arena;
    mtx_t                          arena_lock;
    hashtable_t                    protocols;
    struct rwlock                  protocols_lock;
    hashtable_t                    clients;
    struct rwlock                  clients_lock;
    struct link_table              link_table;
} gracht_server_t;

static struct gracht_message* get_in_buffer_st(struct gracht_server*);
static void                   put_message_st(struct gracht_server*, struct gracht_message*);
static void                   dispatch_st(struct gracht_server*, struct gracht_message*);

static struct server_operations g_stOperations = {
    dispatch_st,
    get_in_buffer_st,
    put_message_st
};

static struct gracht_message* get_in_buffer_mt(struct gracht_server*);
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


static int configure_server(struct gracht_server*, gracht_server_configuration_t*);

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

    status = configure_server(server, config);
    if (status) {
        GRERROR(GRSTR("gracht_server_start: invalid configuration provided"));
        free(server);
        return -1;
    }

    // initialize static members of the instance
    mtx_init(&server->arena_lock, mtx_plain);
    rwlock_init(&server->protocols_lock);
    rwlock_init(&server->clients_lock);
    hashtable_construct(&server->protocols, 0, sizeof(struct gracht_protocol), protocol_hash, protocol_cmp);
    hashtable_construct(&server->clients, 0, sizeof(struct client_wrapper), client_hash, client_cmp);
    stack_construct(&server->bufferStack, 8);

    gracht_server_register_protocol(server, &gracht_control_server_protocol);

    // everything is setup - update state
    server->state = RUNNING;
    *serverOut = server;
    return 0;
}

static int configure_server(struct gracht_server* server, gracht_server_configuration_t* configuration)
{
    size_t arenaSize;
    int    status;

    // set the configuration params that are just transfer
    memcpy(&server->callbacks, &configuration->callbacks, sizeof(struct gracht_server_callbacks));

    // handle the aio descriptor
    if (configuration->set_descriptor_provided) {
        server->set_handle          = configuration->set_descriptor;
        server->set_handle_provided = 1;
    }
    else {
        server->set_handle = gracht_aio_create();
        if (server->set_handle == GRACHT_HANDLE_INVALID) {
            GRERROR(GRSTR("gracht_server: failed to create aio handle"));
            return -1;
        }
    }

    // configure the allocation size, we use the max message size and add
    // 512 bytes for context data
    server->allocationSize = configuration->max_message_size + 512;

    // handle the worker count, if the worker count is not provided we do not use
    // the dispatcher, but instead handle single-threaded.
    if (configuration->server_workers > 1) {
        status = gracht_worker_pool_create(server, configuration->server_workers, &server->worker_pool);
        if (status) {
            GRERROR(GRSTR("configure_server: failed to create the worker pool"));
            return -1;
        }
        server->ops = &g_mtOperations;
    }
    else {
        server->ops = &g_stOperations;
    }

    // handle the max message size override, otherwise we default to our default value.
    if (configuration->server_workers > 1) {
        arenaSize = configuration->server_workers * server->allocationSize * 32;
        status    = gracht_arena_create(arenaSize, &server->arena);
        if (status) {
            GRERROR(GRSTR("configure_server: failed to create the memory pool"));
            return -1;
        }
    }
    else {
        server->recvBuffer = malloc(server->allocationSize);
        if (!server->recvBuffer) {
            GRERROR(GRSTR("configure_server: failed to allocate memory for incoming messages"));
            return -1;
        }
    }

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

    if (tableIndex == GRACHT_SERVER_MAX_LINKS) {
        GRERROR(GRSTR("gracht_server_add_link: the maximum link count was reached"));
        errno = ENOENT;
        return -1;
    }

    connection = link->ops.server.setup(link);
    if (connection == GRACHT_CONN_INVALID) {
        GRERROR(GRSTR("gracht_server_add_link: provided link failed setup"));
        return -1;
    }

    gracht_aio_add(server->set_handle, connection);

    server->link_table.handles[tableIndex] = connection;
    server->link_table.links[tableIndex]   = link;
    return 0;
}

static int handle_connection(struct gracht_server* server, struct gracht_link* link)
{
    struct gracht_server_client* client;

    int status = link->ops.server.accept(link, &client);
    if (status) {
        GRERROR(GRSTR("gracht_server: failed to accept client"));
        return status;
    }

    // this is a streaming client, which means we handle them differently if they should
    // unsubscribe to certain protocols
    client->flags |= GRACHT_CLIENT_FLAG_STREAM;
    
    rwlock_w_lock(&server->clients_lock);
    hashtable_set(&server->clients, &(struct client_wrapper) { 
        .handle = client->handle,
        .link = link,
        .client = client
    });
    rwlock_w_unlock(&server->clients_lock);

    // this is no protected as it should be called only from the same thread. The thread
    // that runs the orchestrator
    gracht_aio_add(server->set_handle, client->handle);

    // invoke the new client callback at last
    if (server->callbacks.clientConnected) {
        server->callbacks.clientConnected(client->handle);
    }
    return 0;
}

static struct gracht_message* get_in_buffer_st(struct gracht_server* server)
{
    struct gracht_message* message = (struct gracht_message*)server->recvBuffer;
    message->server = server;
    return message;
}

static void put_message_st(struct gracht_server* server, struct gracht_message* message)
{
    (void)server;
    (void)message;
    // no op
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
        uint32_t messageLength  = *((uint32_t*)&message->payload[message->index + MSG_INDEX_LEN]);
        uint32_t metaDatalength = sizeof(struct gracht_message) + message->index;
        
        mtx_lock(&server->arena_lock);
        gracht_arena_free(server->arena, message, server->allocationSize - messageLength - metaDatalength);
        mtx_unlock(&server->arena_lock);
        gracht_worker_pool_dispatch(server->worker_pool, message);
    }
}

static struct gracht_message* get_in_buffer_mt(struct gracht_server* server)
{
    struct gracht_message* message;
    mtx_lock(&server->arena_lock);
    message = gracht_arena_allocate(server->arena, NULL, server->allocationSize);
    mtx_unlock(&server->arena_lock);
    message->server = server;
    return message;
}

static void put_message_mt(struct gracht_server* server, struct gracht_message* message)
{
    mtx_lock(&server->arena_lock);
    gracht_arena_free(server->arena, message, server->allocationSize);
    mtx_unlock(&server->arena_lock);
}

static int handle_packet(struct gracht_server* server, struct gracht_link* link)
{
    int status;
    GRTRACE(GRSTR("handle_packet"));
    
    while (1) {
        struct gracht_message* message = server->ops->get_incoming_buffer(server);

        status = link->ops.server.recv_packet(link, message, 0);
        if (status) {
            if (errno != ENODATA) {
                GRERROR(GRSTR("handle_packet link->ops.server.recv_packet returned %i"), errno);
            }
            server->ops->put_message(server, message);
            break;
        }

        server->ops->dispatch(server, message);
    }
    
    return status;
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
    GRTRACE(GRSTR("handle_client_event %i, 0x%x"), handle, events);
    
    // Check for control event. On non-passive sockets, control event is the
    // disconnect event.
    if (events & GRACHT_AIO_EVENT_DISCONNECT) {
        status = gracht_aio_remove(server->set_handle, handle);
        if (status) {
            GRWARNING(GRSTR("handle_client_event: failed to remove descriptor from aio"));
        }
        
        client_destroy(server, handle);
    }
    else if ((events & GRACHT_AIO_EVENT_IN) || !events) {
        struct client_wrapper* entry;
        
        rwlock_r_lock(&server->clients_lock);
        entry = hashtable_get(&server->clients, &(struct client_wrapper){ .handle = handle });
        while (entry) {
            struct gracht_message* message = server->ops->get_incoming_buffer(server);
            if (!message) {
                GRERROR(GRSTR("handle_client_event ran out of receiving buffers"));
                errno = ENOMEM;
                return -1;
            }
            
            status = entry->link->ops.server.recv_client(entry->client, message, 0);
            if (status) {
                if (errno != ENODATA && errno != EAGAIN) {
                    GRERROR(GRSTR("handle_client_event server_object.link->recv_client returned %i"), errno);
                }
                server->ops->put_message(server, message);
                break;
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
    hashtable_enumerate(&server->clients, client_enum_destroy, NULL);
    rwlock_w_unlock(&server->clients_lock);

    // destroy all our links
    for (i = 0; i < GRACHT_SERVER_MAX_LINKS; i++) {
        if (server->link_table.links[i]) {
            server->link_table.links[i]->ops.server.destroy(server->link_table.links[i]);
            server->link_table.links[i] = NULL;
        }
    }
    
    // destroy the event descriptor
    if (server->set_handle != GRACHT_HANDLE_INVALID && !server->set_handle_provided) {
        gracht_aio_destroy(server->set_handle);
    }

    // iterate all our serializer buffers and destroy them
    buffer = stack_pop(&server->bufferStack);
    while (buffer) {
        free(buffer);
        buffer = stack_pop(&server->bufferStack);
    }

    // destroy all our allocated resources
    if (server->arena) {
        gracht_arena_destroy(server->arena);
    }
    
    if (server->recvBuffer) {
        free(server->recvBuffer);
    }

    stack_destroy(&server->bufferStack);
    hashtable_destroy(&server->protocols);
    hashtable_destroy(&server->clients);
    mtx_destroy(&server->arena_lock);
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

    mtx_lock(&server->arena_lock);
    gracht_arena_free(server->arena, recvMessage, 0);
    mtx_unlock(&server->arena_lock);
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
        int num_events = gracht_io_wait(server->set_handle, &events[0], 32);
        GRTRACE(GRSTR("gracht_server: %i events received!"), num_events);
        for (i = 0; i < num_events; i++) {
            gracht_conn_t handle = gracht_aio_event_handle(&events[i]);
            uint32_t      flags  = gracht_aio_event_events(&events[i]);

            GRTRACE(GRSTR("gracht_server: event %u from %i"), flags, handle);
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

    data = stack_pop(&server->bufferStack);
    if (!data) {
        data = malloc(server->allocationSize);
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

int gracht_server_respond(struct gracht_message* messageContext, gracht_buffer_t* message)
{
    struct client_wrapper* entry;
    int                    status;

    if (!messageContext || !message) {
        GRERROR(GRSTR("gracht_server: null message or context"));
        errno = EINVAL;
        return -1;
    }

    // update message header
    GB_MSG_ID_0(message)  = *((uint32_t*)&messageContext->payload[messageContext->index]);
    GB_MSG_LEN_0(message) = message->index;

    rwlock_r_lock(&messageContext->server->clients_lock);
    entry = hashtable_get(&messageContext->server->clients, &(struct client_wrapper){ .handle = messageContext->client });
    if (!entry) {
        struct gracht_link* link;
        
        rwlock_r_unlock(&messageContext->server->clients_lock);
        link = get_link_by_conn(messageContext->server, messageContext->link);
        if (!link) {
            errno = ENODEV;
            return -1;
        }
        status = link->ops.server.respond(link, messageContext, message);
    }
    else {
        status = entry->link->ops.server.send_client(entry->client, message, GRACHT_MESSAGE_BLOCK);
    }
    rwlock_r_unlock(&messageContext->server->clients_lock);

    // return the borrowed buffer to the stack
    stack_push(&messageContext->server->bufferStack, message->data);
    return status;
}

int gracht_server_send_event(gracht_server_t* server, gracht_conn_t client, gracht_buffer_t* message, unsigned int flags)
{
    struct client_wrapper* clientEntry;
    int                    status;

    if (!server || !message) {
        errno = EINVAL;
        return -1;
    }

    // update message header
    GB_MSG_LEN_0(message) = message->index;

    rwlock_r_lock(&server->clients_lock);
    clientEntry = hashtable_get(&server->clients, &(struct client_wrapper){ .handle = client });
    if (!clientEntry) {
        rwlock_r_unlock(&server->clients_lock);
        errno = ENOENT;
        return -1;
    }
   
    // When sending target specific events - we do not care about subscriptions
    status = clientEntry->link->ops.server.send_client(clientEntry->client, message, flags);
    rwlock_r_unlock(&server->clients_lock);

    // return the borrowed buffer to the stack
    stack_push(&server->bufferStack, message->data);
    return status;
}

int gracht_server_broadcast_event(gracht_server_t* server, gracht_buffer_t* message, unsigned int flags)
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
    hashtable_enumerate(&server->clients, client_enum_broadcast, &context);
    rwlock_r_unlock(&server->clients_lock);

    // return the borrowed buffer to the stack
    stack_push(&server->bufferStack, message->data);
    return 0;
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
    if (hashtable_get(&server->protocols, protocol)) {
        rwlock_w_unlock(&server->protocols_lock);
        errno = EEXIST;
        return -1;
    }
    hashtable_set(&server->protocols, protocol);
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
    hashtable_remove(&server->protocols, protocol);
    rwlock_w_unlock(&server->protocols_lock);
}

gracht_conn_t gracht_server_get_dgram_iod(gracht_server_t* server)
{
    if (!server) {
        errno = EINVAL;
        return GRACHT_CONN_INVALID;
    }

    if (server->state != RUNNING) {
        errno = EPERM;
        return GRACHT_CONN_INVALID;
    }

    for (int i = 0; i < GRACHT_SERVER_MAX_LINKS; i++) {
        if (server->link_table.links[i] &&
            server->link_table.links[i]->type == gracht_link_packet_based) {
                return server->link_table.handles[i];
        }
    }

    errno = ENOENT;
    return GRACHT_CONN_INVALID;
}

gracht_handle_t gracht_server_get_set_iod(gracht_server_t* server)
{
    if (!server) {
        errno = EINVAL;
        return GRACHT_CONN_INVALID;
    }

    if (server->state != RUNNING) {
        errno = EPERM;
        return GRACHT_CONN_INVALID;
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
    entry = hashtable_remove(&server->clients, &(struct client_wrapper){ .handle = client });
    if (entry) {
        entry->link->ops.server.destroy_client(entry->client);
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
    
    // When dealing with connectionless clients, they aren't really created in the client register. To deal
    // with this, we actually create a record for them, so we can support connection-less events. This means
    // that connection-less clients aren't considered connected unless they subscribe to some protocol - even
    // if they actually use the functions provided by the protocol. It is also possible to receive targetted
    // events that come in response to a function call even without subscribing.
    rwlock_r_lock(&message->server->clients_lock);
    entry = hashtable_get(&message->server->clients, &(struct client_wrapper){ .handle = message->client });
    if (!entry) {
        struct client_wrapper newEntry;

        // So, client did not have a record, at this point we then know this message was received on a 
        // connection-less stream, meaning we do not currently hold another _read_lock on this thread, thus we can
        // release our reader lock and acqurie the write
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
        hashtable_set(&message->server->clients, &newEntry);
        rwlock_w_unlock(&message->server->clients_lock);

        if (message->server->callbacks.clientConnected) {
            message->server->callbacks.clientConnected(message->client);
        }

        // not really neccessary but for correctness
        rwlock_r_lock(&message->server->clients_lock);
    }
    else {
        // make sure if they were marked cleanup that we remove that
        entry->client->flags &= ~(GRACHT_CLIENT_FLAG_CLEANUP);
    }

    client_subscribe(entry->client, protocol);
    rwlock_r_unlock(&message->server->clients_lock);
}

void gracht_control_unsubscribe_invocation(const struct gracht_message* message, const uint8_t protocol)
{
    struct client_wrapper* entry;
    int                    cleanup;
    
    rwlock_r_lock(&message->server->clients_lock);
    entry = hashtable_get(&message->server->clients, &(struct client_wrapper){ .handle = message->client });
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
    // after handling messages whether or not a client has been marked for cleanup
    // in this case we do not hold the lock and can therefore actually take the write lock
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
    uint8_t                      protocol = GB_MSG_SID(context->message);
    (void)index;

    if (client_is_subscribed(entry->client, protocol)) {
        entry->link->ops.server.send_client(entry->client, context->message, context->flags);
    }
}

static void client_enum_destroy(int index, const void* element, void* userContext)
{
    const struct client_wrapper* entry = element;
    (void)index;
    (void)userContext;
    entry->link->ops.server.destroy_client(entry->client);
}
