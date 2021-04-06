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
#include "include/aio.h"
#include "include/debug.h"
#include "include/list.h"
#include "include/gracht/server.h"
#include "include/gracht/link/link.h"
#include "include/thread_api.h"
#include "include/utils.h"
#include "include/server_private.h"
#include <stdlib.h>
#include <string.h>

GRACHT_STRUCT(gracht_subscription_args, {
    uint8_t protocol_id;
});

static void gracht_control_subscribe_callback(struct gracht_recv_message* message, struct gracht_subscription_args*);
static void gracht_control_unsubscribe_callback(struct gracht_recv_message* message, struct gracht_subscription_args*);

static gracht_protocol_function_t control_functions[2] = {
   { 0 , gracht_control_subscribe_callback },
   { 1 , gracht_control_unsubscribe_callback },
};
static gracht_protocol_t control_protocol = GRACHT_PROTOCOL_INIT(0, "gctrl", 2, control_functions);

struct gracht_server {
    struct server_link_ops*        ops;
    struct gracht_server_callbacks callbacks;
    struct gracht_arena*           arena;
    struct gracht_worker_pool*     worker_pool;
    void                          (*dispatch)(struct gracht_server*, struct gracht_recv_message*);
    struct gracht_recv_message*   (*get_message)(struct gracht_server*);
    size_t                         allocationSize;
    void*                          messageBuffer;
    int                            initialized;
    gracht_handle_t                set_handle;
    int                            set_handle_provided;
    gracht_conn_t                  listen_handle;
    gracht_conn_t                  dgram_handle;
    mtx_t                          sync_object;
    struct gracht_list             protocols;
    struct gracht_list             clients;
} g_grachtServer = {
        NULL,
        { NULL, NULL },
        NULL,
        NULL,
        NULL,
        NULL,
        0,
        NULL,
        0,
        GRACHT_HANDLE_INVALID,
        0,
        GRACHT_HANDLE_INVALID,
        GRACHT_HANDLE_INVALID,
        { { 0 } },
        { 0 },
        { 0 }
};

static void client_destroy(struct gracht_server_client*);
static void client_subscribe(struct gracht_server_client*, uint8_t);
static void client_unsubscribe(struct gracht_server_client*, uint8_t);
static int  client_is_subscribed(struct gracht_server_client*, uint8_t);

static struct gracht_recv_message* get_message_st(struct gracht_server*);
static struct gracht_recv_message* get_message_mt(struct gracht_server*);
static void                        dispatch_st(struct gracht_server*, struct gracht_recv_message*);
static void                        dispatch_mt(struct gracht_server*, struct gracht_recv_message*);

static int configure_server(struct gracht_server*, gracht_server_configuration_t*);
static int create_links(struct gracht_server*);

int gracht_server_initialize(gracht_server_configuration_t* configuration)
{
    int status;
    if (g_grachtServer.initialized) {
        errno = EALREADY;
        return -1;
    }

    if (!configuration) {
        errno = EINVAL;
        return -1;
    }

    mtx_init(&g_grachtServer.sync_object, mtx_plain);
    status = configure_server(&g_grachtServer, configuration);
    if (status) {
        GRERROR("gracht_server_initialize: invalid configuration provided\n");
        return -1;
    }

    status = create_links(&g_grachtServer);
    if (status) {
        GRERROR("gracht_server_initialize: failed to initialize underlying links\n");
        return -1;
    }

    gracht_server_register_protocol(&control_protocol);
    g_grachtServer.initialized = 1;
    return 0;
}

static int configure_server(struct gracht_server* server, gracht_server_configuration_t* configuration)
{
    size_t arenaSize;
    int    status;

    // set the configuration params that are just transfer
    server->ops = configuration->link;
    memcpy(&server->callbacks, &configuration->callbacks, sizeof(struct gracht_server_callbacks));

    // handle the aio descriptor
    if (configuration->set_descriptor_provided) {
        server->set_handle          = configuration->set_descriptor;
        server->set_handle_provided = 1;
    }
    else {
        server->set_handle = gracht_aio_create();
        if (server->set_handle == GRACHT_HANDLE_INVALID) {
            GRERROR("gracht_server: failed to create aio handle\n");
            return -1;
        }
    }

    // handle the worker count, if the worker count is not provided we do not use
    // the dispatcher, but instead handle single-threaded.
    if (configuration->server_workers > 1) {
        status = gracht_worker_pool_create(server, configuration->server_workers, &server->worker_pool);
        if (status) {
            GRERROR("configure_server: failed to create the worker pool");
            return -1;
        }
        server->dispatch = dispatch_mt;
    }
    else {
        server->dispatch = dispatch_st;
    }

    // configure the allocation size, we use the max message size and add
    // 512 bytes for context data
    server->allocationSize = configuration->max_message_size + 512;

    // handle the max message size override, otherwise we default to our default value.
    if (configuration->server_workers > 1) {
        arenaSize = configuration->server_workers * server->allocationSize * 32;
        status    = gracht_arena_create(arenaSize, &server->arena);
        if (status) {
            GRERROR("configure_server: failed to create the memory pool");
            return -1;
        }
        server->get_message = get_message_mt;
    }
    else {
        server->messageBuffer = malloc(server->allocationSize);
        if (!server->messageBuffer) {
            GRERROR("configure_server: failed to allocate memory for messages");
            return -1;
        }
        server->get_message = get_message_st;
    }

    return 0;
}

static int create_links(struct gracht_server* server)
{
    // try to create the listening link. We do support that one of the links
    // are not supported by the link operations.
    server->listen_handle = server->ops->listen(server->ops, LINK_LISTEN_SOCKET);
    if (server->listen_handle == GRACHT_HANDLE_INVALID) {
        if (errno != ENOTSUP) {
            return -1;
        }
    }
    else {
        gracht_aio_add(server->set_handle, server->listen_handle);
    }

    server->dgram_handle = server->ops->listen(server->ops, LINK_LISTEN_DGRAM);
    if (server->dgram_handle == GRACHT_HANDLE_INVALID) {
        if (errno != ENOTSUP) {
            return -1;
        }
    }
    else {
        gracht_aio_add(server->set_handle, server->dgram_handle);
    }

    if (server->listen_handle == GRACHT_HANDLE_INVALID && server->dgram_handle == GRACHT_HANDLE_INVALID) {
        GRERROR("create_links: neither of client and dgram links were supported");
        return -1;
    }
    return 0;
}

static int handle_client_socket(struct gracht_server* server)
{
    struct gracht_server_client* client;

    int status = server->ops->accept(server->ops, &client);
    if (status) {
        GRERROR("gracht_server: failed to accept client\n");
        return status;
    }
    
    gracht_list_append(&server->clients, &client->header);
    gracht_aio_add(server->set_handle, client->handle);

    // invoke the new client callback at last
    if (server->callbacks.clientConnected) {
        server->callbacks.clientConnected(client->handle);
    }
    return 0;
}

static struct gracht_recv_message* get_message_st(struct gracht_server* server)
{
    return (struct gracht_recv_message*)server->messageBuffer;
}

static void dispatch_st(struct gracht_server* server, struct gracht_recv_message* message)
{
    int status = server_invoke_action(server, message);
    if (status) {
        GRWARNING("[dispatch_st] failed to invoke server action\n");
    }
}

static void dispatch_mt(struct gracht_server* server, struct gracht_recv_message* message)
{
    // todo we need the message size to shrink the allocation before dispatching
    // gracht_arena_free(server->arena, message, MISSING);
    gracht_worker_pool_dispatch(server->worker_pool, message);
}

static struct gracht_recv_message* get_message_mt(struct gracht_server* server)
{
    return (struct gracht_recv_message*)gracht_arena_allocate(server->arena, NULL, server->allocationSize);
}

static int handle_sync_event(struct gracht_server* server)
{
    struct gracht_recv_message* message = server->get_message(server);
    int                         status;
    GRTRACE("[handle_sync_event]");
    
    while (1) {
        status = server->ops->recv_packet(server->ops, message, 0);
        if (status) {
            if (errno != ENODATA) {
                GRERROR("[handle_sync_event] server_object.ops->recv_packet returned %i\n", errno);
            }
            break;
        }
        server->dispatch(server, message);
    }
    
    return status;
}

static int handle_async_event(struct gracht_server* server, gracht_handle_t handle, uint32_t events)
{
    int                          status;
    struct gracht_recv_message*  message = server->get_message(server);
    struct gracht_server_client* client = 
        (struct gracht_server_client*)gracht_list_lookup(&server->clients, handle);
    GRTRACE("[handle_async_event] %i, 0x%x\n", handle, events);
    
    // Check for control event. On non-passive sockets, control event is the
    // disconnect event.
    if (events & GRACHT_AIO_EVENT_DISCONNECT) {
        status = gracht_aio_remove(server->set_handle, handle);
        if (status) {
            GRWARNING("handle_async_event: failed to remove descriptor from aio");
        }
        
        client_destroy(client);
    }
    else if ((events & GRACHT_AIO_EVENT_IN) || !events) {
        while (1) {
            status = server->ops->recv_client(client, message, 0);
            if (status) {
                if (errno != ENODATA) {
                    GRERROR("[handle_async_event] server_object.ops->recv_client returned %i\n", errno);
                }
                break;
            }

            server->dispatch(server, message);
        }
    }
    return 0;
}

static int gracht_server_shutdown(void)
{
    struct gracht_server_client* client;
    
    if (!g_grachtServer.initialized) {
        errno = ENOTSUP;
        return -1;
    }
    
    client = (struct gracht_server_client*)g_grachtServer.clients.head;
    while (client) {
        struct gracht_server_client* temp = (struct gracht_server_client*)client->header.link;
        client_destroy(client);
        client = temp;
    }
    g_grachtServer.clients.head = NULL;
    
    if (g_grachtServer.set_handle != GRACHT_HANDLE_INVALID && !g_grachtServer.set_handle_provided) {
        gracht_aio_destroy(g_grachtServer.set_handle);
    }

    if (g_grachtServer.messageBuffer) {
        free(g_grachtServer.messageBuffer);
    }
    
    if (g_grachtServer.ops != NULL) {
        g_grachtServer.ops->destroy(g_grachtServer.ops);
    }

    g_grachtServer.initialized = 0;
    return 0;
}

int server_invoke_action(struct gracht_server* server, struct gracht_recv_message* recvMessage)
{
    gracht_protocol_function_t* function;
    void*                       param_storage;
    
    mtx_lock(&server->sync_object);
    function = get_protocol_action(&server->protocols, recvMessage->protocol, recvMessage->action);
    mtx_unlock(&server->sync_object);
    if (!function) {
        // TODO send invalid invocation response
        return -1;
    }
    
    param_storage = ((char*)recvMessage->params + (recvMessage->param_count * sizeof(struct gracht_param)));
    
    GRTRACE("server_invoke_action: offset=%lu, param_count=%i\n",
        recvMessage->param_count * sizeof(struct gracht_param), 
        recvMessage->param_count);
    
    if (recvMessage->param_in) {
        uint8_t* unpackBuffer = alloca(recvMessage->param_in * sizeof(void*)); // security risk
        unpack_parameters(recvMessage->params, recvMessage->param_in, param_storage, &unpackBuffer[0]);
        ((server_invokeA0_t)function->address)(recvMessage, &unpackBuffer[0]);
    }
    else {
        ((server_invoke00_t)function->address)(recvMessage);
    }
    return 0;
}

void server_cleanup_message(struct gracht_server* server, struct gracht_recv_message* recvMessage)
{
    if (!server || !recvMessage) {
        return;
    }

    mtx_lock(&server->sync_object);
    gracht_arena_free(server->arena, recvMessage, 0);
    mtx_unlock(&server->sync_object);
}

int gracht_server_handle_event(gracht_handle_t handle, unsigned int events)
{
    if (handle == g_grachtServer.listen_handle) {
        return handle_client_socket(&g_grachtServer);
    }
    else if (handle == g_grachtServer.dgram_handle) {
        return handle_sync_event(&g_grachtServer);
    }
    else {
        return handle_async_event(&g_grachtServer, handle, events);
    }
}

int gracht_server_main_loop(void)
{
    gracht_aio_event_t events[32];
    int                i;

    GRTRACE("gracht_server: started... [%i, %i]\n", g_grachtServer.listen_handle, g_grachtServer.dgram_handle);
    while (g_grachtServer.initialized) {
        int num_events = gracht_io_wait(g_grachtServer.set_handle, &events[0], 32);
        GRTRACE("gracht_server: %i events received!\n", num_events);
        for (i = 0; i < num_events; i++) {
            gracht_handle_t handle = gracht_aio_event_handle(&events[i]);
            uint32_t        flags  = gracht_aio_event_events(&events[i]);

            GRTRACE("gracht_server: event %u from %i\n", flags, handle);
            gracht_server_handle_event(handle, flags);
        }
    }

    return gracht_server_shutdown();
}

int gracht_server_respond(struct gracht_recv_message* messageContext, struct gracht_message* message)
{
    struct gracht_server_client* client;

    if (!messageContext || !message) {
        GRERROR("gracht_server: null message or context");
        errno = (EINVAL);
        return -1;
    }

    // update the id for the response
    message->header.id = messageContext->message_id;

    client = (struct gracht_server_client*)gracht_list_lookup(&g_grachtServer.clients, messageContext->client);
    if (!client) {
        return g_grachtServer.ops->respond(g_grachtServer.ops, messageContext, message);
    }

    return g_grachtServer.ops->send_client(client, message, GRACHT_MESSAGE_BLOCK);
}

int gracht_server_send_event(int client, struct gracht_message* message, unsigned int flags)
{
    struct gracht_server_client* serverClient = 
        (struct gracht_server_client*)gracht_list_lookup(&g_grachtServer.clients, client);
    if (!serverClient) {
        errno = (ENOENT);
        return -1;
    }
    
    // When sending target specific events - we do not care about subscriptions
    return g_grachtServer.ops->send_client(serverClient, message, flags);
}

int gracht_server_broadcast_event(struct gracht_message* message, unsigned int flags)
{
    struct gracht_server_client* client;
    
    client = (struct gracht_server_client*)g_grachtServer.clients.head;
    while (client) {
        if (client_is_subscribed(client, message->header.protocol)) {
            g_grachtServer.ops->send_client(client, message, flags);
        }
        client = (struct gracht_server_client*)client->header.link;
    }
    return 0;
}

int gracht_server_register_protocol(gracht_protocol_t* protocol)
{
    if (!protocol) {
        errno = (EINVAL);
        return -1;
    }
    
    gracht_list_append(&g_grachtServer.protocols, &protocol->header);
    return 0;
}

void gracht_server_unregister_protocol(gracht_protocol_t* protocol)
{
    if (!protocol) {
        return;
    }
    
    gracht_list_remove(&g_grachtServer.protocols, &protocol->header);
}

gracht_handle_t gracht_server_get_dgram_iod(void)
{
    return g_grachtServer.dgram_handle;
}

gracht_handle_t gracht_server_get_set_iod(void)
{
    return g_grachtServer.set_handle;
}

// Client helpers
static void client_destroy(struct gracht_server_client* client)
{
    if (g_grachtServer.callbacks.clientDisconnected) {
        g_grachtServer.callbacks.clientDisconnected(client->handle);
    }

    gracht_list_remove(&g_grachtServer.clients, &client->header);
    g_grachtServer.ops->destroy_client(client);
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
void gracht_control_subscribe_callback(struct gracht_recv_message* message, struct gracht_subscription_args* input)
{
    struct gracht_server_client* client = 
        (struct gracht_server_client*)gracht_list_lookup(&g_grachtServer.clients, message->client);
    if (!client) {
        if (g_grachtServer.ops->create_client(g_grachtServer.ops, message, &client)) {
            GRERROR("[gracht_control_subscribe_callback] server_object.ops->create_client returned error");
            return;
        }
        gracht_list_append(&g_grachtServer.clients, &client->header);

        if (g_grachtServer.callbacks.clientConnected) {
            g_grachtServer.callbacks.clientConnected(client->handle);
        }
    }

    client_subscribe(client, input->protocol_id);
}

void gracht_control_unsubscribe_callback(struct gracht_recv_message* message, struct gracht_subscription_args* input)
{
    struct gracht_server_client* client = 
        (struct gracht_server_client*)gracht_list_lookup(&g_grachtServer.clients, message->client);
    if (!client) {
        return;
    }

    client_unsubscribe(client, input->protocol_id);
    
    // cleanup the client if we unsubscripe
    if (input->protocol_id == 0xFF) {
        client_destroy(client);
    }
}
