/* MollenOS
 *
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
 * Gracht Socket Link Type Definitions & Structures
 * - This header describes the base link-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <assert.h>
#include <errno.h>
#include "../include/gracht/link/socket.h"
#include "../include/debug.h"
#include "../include/crc.h"
#include "../include/server_private.h"
#include <stdlib.h>
#include <string.h>

#include "socket_os.h"

struct socket_link_client {
    struct gracht_server_client base;
    struct sockaddr_storage     address;
    int                         socket;
};

struct socket_link_manager {
    struct server_link_ops             ops;
    struct socket_server_configuration config;
    
    gracht_conn_t client_socket;
    gracht_conn_t dgram_socket;
};

static unsigned int get_socket_flags(unsigned int flags)
{
    unsigned int socketFlags = 0;
    if (!(flags & GRACHT_MESSAGE_BLOCK)) {
        socketFlags |= MSG_DONTWAIT;
    }
    if (flags & GRACHT_MESSAGE_WAITALL) {
        socketFlags |= MSG_WAITALL;
    }
    return socketFlags;
}

static int socket_link_send_client(struct socket_link_client* client,
    struct gracht_message* message, unsigned int flags)
{
    i_iobuf_t*    iov = alloca(sizeof(i_iobuf_t) * (1 + message->header.param_in));
    unsigned int  socketFlags = get_socket_flags(flags);
    uint32_t      i;
    int           iovCount = 1;
    intmax_t      bytesWritten;
    i_msghdr_t    msg = I_MSGHDR_INIT;
    
    // Prepare the header
    i_iobuf_set_buf(&iov[0], message);
    i_iobuf_set_len(&iov[0], sizeof(struct gracht_message) + (
        (message->header.param_in + message->header.param_out) * sizeof(struct gracht_param)));
    
    // Prepare the parameters
    for (i = 0; i < message->header.param_in; i++) {
        if (message->params[i].type == GRACHT_PARAM_BUFFER) {
            i_iobuf_set_buf(&iov[iovCount], message->params[i].data.buffer);
            i_iobuf_set_len(&iov[iovCount], message->params[i].length);
            iovCount++;
        }
        else if (message->params[i].type == GRACHT_PARAM_SHM) {
            // NO SUPPORT
            assert(0);
        }
    }

    i_msghdr_set_bufs(&msg, &iov[0], iovCount);

    GRTRACE(GRSTR("[socket_link_send] sending message"));
    bytesWritten = sendmsg(client->base.handle, &msg, socketFlags);
    if (bytesWritten != message->header.length) {
        return -1;
    }

    return 0;
}

static int socket_link_recv_client(struct socket_link_client* client,
    struct gracht_recv_message* context, unsigned int flags)
{
    struct gracht_message* message        = (struct gracht_message*)&context->payload[0];
    char*                  params_storage = NULL;
    unsigned int           socketFlags    = get_socket_flags(flags);
    intmax_t               bytes_read;
    
    GRTRACE(GRSTR("[gracht_connection_recv_stream] reading message header"));
    bytes_read = recv(client->base.handle, message, sizeof(struct gracht_message), socketFlags);
    if (bytes_read != sizeof(struct gracht_message)) {
        if (bytes_read == 0) {
            errno = (ENODATA);
        }
        return -1;
    }
    
    if (message->header.param_in) {
        intmax_t bytesToRead = message->header.length - sizeof(struct gracht_message);

        GRTRACE(GRSTR("[gracht_connection_recv_stream] reading message payload"));
        params_storage = (char*)&context->payload[sizeof(struct gracht_message)];
        bytes_read     = recv(client->base.handle, params_storage, (size_t)bytesToRead, MSG_WAITALL);
        if (bytes_read != bytesToRead) {
            // do not process incomplete requests
            // TODO error code / handling
            GRERROR(GRSTR("[gracht_connection_recv_message] did not read full amount of bytes (%u, expected %u)"),
                  (uint32_t)bytes_read, (uint32_t)(message->header.length - sizeof(struct gracht_message)));
            errno = (EPIPE);
            return -1;
        }
    }

    context->message_id  = message->header.id;
    context->client      = client->socket;
    context->params      = (void*)params_storage;
    
    context->param_in    = message->header.param_in;
    context->param_count = message->header.param_in + message->header.param_out;
    context->protocol    = message->header.protocol;
    context->action      = message->header.action;
    return 0;
}

static int socket_link_create_client(struct socket_link_manager* linkManager, struct gracht_recv_message* message,
    struct socket_link_client** clientOut)
{
    struct socket_link_client* client;
    struct sockaddr_storage*   address;
    
    if (!linkManager || !message || !clientOut) {
        errno = (EINVAL);
        return -1;
    }
    
    client = (struct socket_link_client*)malloc(sizeof(struct socket_link_client));
    if (!client) {
        errno = (ENOMEM);
        return -1;
    }

    memset(client, 0, sizeof(struct socket_link_client));
    client->base.handle = message->client;
    client->socket      = linkManager->dgram_socket;

    address = (struct sockaddr_storage*)&message->payload[0];
    memcpy(&client->address, address, (size_t)linkManager->config.server_address_length);
    
    *clientOut = client;
    return 0;
}

static int socket_link_destroy_client(struct socket_link_client* client)
{
    int status;
    
    if (!client) {
        errno = (EINVAL);
        return -1;
    }
    
    status = close(client->base.handle);
    free(client);
    return status;
}

static gracht_conn_t socket_link_listen(struct socket_link_manager* linkManager, int mode)
{
    int status;
    
    if (mode == LINK_LISTEN_DGRAM) {
        // Create a new socket for listening to events. They are all
        // delivered to fixed sockets on the local system.
        linkManager->dgram_socket = socket(AF_LOCAL, SOCK_DGRAM, 0);
        if (linkManager->dgram_socket < 0) {
            return GRACHT_CONN_INVALID;
        }
        
        status = bind(linkManager->dgram_socket,
            (const struct sockaddr*)&linkManager->config.dgram_address,
            linkManager->config.dgram_address_length);
        if (status) {
            return GRACHT_CONN_INVALID;
        }
        
        return linkManager->dgram_socket;
    }
    else if (mode == LINK_LISTEN_SOCKET) {
        linkManager->client_socket = socket(AF_LOCAL, SOCK_STREAM, 0);
        if (linkManager->client_socket < 0) {
            return GRACHT_CONN_INVALID;
        }
        
        status = bind(linkManager->client_socket,
            (const struct sockaddr*)&linkManager->config.server_address,
            linkManager->config.server_address_length);
        if (status) {
            return GRACHT_CONN_INVALID;
        }
        
        // Enable listening for connections, with a maximum of 2 on backlog
        status = listen(linkManager->client_socket, 2);
        if (status) {
            return GRACHT_CONN_INVALID;
        }
        
        return linkManager->client_socket;
    }
    
    errno = (ENOTSUP);
    return GRACHT_CONN_INVALID;
}

static int socket_link_accept(struct socket_link_manager* linkManager, struct gracht_server_client** clientOut)
{
    struct socket_link_client* client;
    socklen_t                  address_length;
    GRTRACE(GRSTR("[socket_link_accept]"));

    client = (struct socket_link_client*)malloc(sizeof(struct socket_link_client));
    if (!client) {
        GRERROR(GRSTR("link_server: failed to allocate data for link"));
        errno = (ENOMEM);
        return -1;
    }

    memset(client, 0, sizeof(struct socket_link_client));

    // TODO handle disconnects in accept in netmanager
    client->socket = accept(linkManager->client_socket, (struct sockaddr*)&client->address, &address_length);
    if (client->socket < 0) {
        GRERROR(GRSTR("link_server: failed to accept client"));
        free(client);
        return -1;
    }
    client->base.handle = client->socket;
    
    *clientOut = &client->base;
    return 0;
}

static int socket_link_recv_packet(struct socket_link_manager* linkManager, 
    struct gracht_recv_message* context, unsigned int flags)
{
    struct gracht_message* message        = (struct gracht_message*)&context->payload[linkManager->config.dgram_address_length];
    void*                  params_storage = NULL;
    unsigned int           socketFlags    = get_socket_flags(flags);
    uint32_t               addressCrc;
    i_iobuf_t              iov[1];
    i_msghdr_t             msg = I_MSGHDR_INIT;

    i_iobuf_set_buf(&iov[0], message);
    i_iobuf_set_len(&iov[0], (size_t)(GRACHT_MAX_MESSAGE_SIZE - linkManager->config.dgram_address_length));

    i_msghdr_set_addr(&msg, &context->payload[0], linkManager->config.dgram_address_length);
    i_msghdr_set_bufs(&msg, &iov[0], 1);
    
    // Packets are atomic, either the full packet is there, or none is. So avoid
    // the use of MSG_WAITALL here.
    intmax_t bytes_read = recvmsg(linkManager->dgram_socket, &msg, socketFlags);
    if (bytes_read <= 0) {
        if (bytes_read == 0) {
            errno = (ENODATA);
        }
        return -1;
    }

    addressCrc = crc32_generate((const unsigned char*)i_msghdr_addr_name(&msg), (size_t)i_msghdr_addr_len(&msg));
    GRTRACE(GRSTR("[gracht_connection_recv_stream] read [%u/%u] addr bytes, %p"),
            i_msghdr_addr_len(&msg), linkManager->config.dgram_address_length,
            i_msghdr_addr_name(&msg));
    GRTRACE(GRSTR("[gracht_connection_recv_stream] read %lu bytes, %u"), bytes_read, i_msghdr_flags(&msg));
    GRTRACE(GRSTR("[gracht_connection_recv_stream] parameter offset %lu"), (uintptr_t)&message->params[0] - (uintptr_t)message);
    if (message->header.param_in) {
        params_storage = &message->params[0];
    }

    context->message_id  = message->header.id;
    context->client      = (int)addressCrc;
    context->params      = params_storage;

    context->param_in    = message->header.param_in;
    context->param_count = message->header.param_in + message->header.param_out;
    context->protocol    = message->header.protocol;
    context->action      = message->header.action;
    return 0;
}

static int socket_link_respond(struct socket_link_manager* linkManager,
    struct gracht_recv_message* messageContext, struct gracht_message* message)
{
    i_iobuf_t*    iov = alloca(sizeof(i_iobuf_t) * (1 + message->header.param_in));
    uint32_t      i;
    int           iovCount = 1;
    intmax_t      bytesWritten;
    i_msghdr_t    msg = I_MSGHDR_INIT;

    // Prepare the header
    i_iobuf_set_buf(&iov[0], message);
    i_iobuf_set_len(&iov[0], sizeof(struct gracht_message) + (
        (message->header.param_in + message->header.param_out) * sizeof(struct gracht_param)));
    
    // Prepare the parameters
    for (i = 0; i < message->header.param_in; i++) {
        if (message->params[i].type == GRACHT_PARAM_BUFFER) {
            i_iobuf_set_buf(&iov[iovCount], message->params[i].data.buffer);
            i_iobuf_set_len(&iov[iovCount], message->params[i].length);
            iovCount++;
        }
        else if (message->params[i].type == GRACHT_PARAM_SHM) {
            // NO SUPPORT
            assert(0);
        }
    }
    
    i_msghdr_set_addr(&msg, &messageContext->payload[0], linkManager->config.dgram_address_length);
    i_msghdr_set_bufs(&msg, &iov[0], iovCount);

    bytesWritten = sendmsg(linkManager->dgram_socket, &msg, MSG_WAITALL);
    if (bytesWritten != message->header.length) {
        GRERROR(GRSTR("link_server: failed to respond [%li/%i]"), bytesWritten, message->header.length);
        if (bytesWritten == -1) {
            GRERROR(GRSTR("link_server: errno %i"), errno);
        }
        return -1;
    }

    return 0;
}

static void socket_link_destroy(struct socket_link_manager* linkManager)
{
    if (!linkManager) {
        return;
    }
    
    if (linkManager->dgram_socket > 0) {
        close(linkManager->dgram_socket);
    }
    
    if (linkManager->client_socket > 0) {
        close(linkManager->client_socket);
    }
    
    free(linkManager);
}

int gracht_link_socket_server_create(struct server_link_ops** linkOut, 
    struct socket_server_configuration* configuration)
{
    struct socket_link_manager* linkManager;
    
    linkManager = (struct socket_link_manager*)malloc(sizeof(struct socket_link_manager));
    if (!linkManager) {
        errno = (ENOMEM);
        return -1;
    }
    
    memset(linkManager, 0, sizeof(struct socket_link_manager));
    memcpy(&linkManager->config, configuration, sizeof(struct socket_server_configuration));
    
    linkManager->ops.create_client  = (server_create_client_fn)socket_link_create_client;
    linkManager->ops.destroy_client = (server_destroy_client_fn)socket_link_destroy_client;

    linkManager->ops.recv_client = (server_recv_client_fn)socket_link_recv_client;
    linkManager->ops.send_client = (server_send_client_fn)socket_link_send_client;

    linkManager->ops.listen      = (server_link_listen_fn)socket_link_listen;
    linkManager->ops.accept      = (server_link_accept_fn)socket_link_accept;
    linkManager->ops.recv_packet = (server_link_recv_packet_fn)socket_link_recv_packet;
    linkManager->ops.respond     = (server_link_respond_fn)socket_link_respond;
    linkManager->ops.destroy     = (server_link_destroy_fn)socket_link_destroy;
    
    *linkOut = &linkManager->ops;
    return 0;
}
