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
 * Gracht Socket Link Type Definitions & Structures
 * - This header describes the base link-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <assert.h>
#include <errno.h>
#include "gracht/link/socket.h"
#include "debug.h"
#include "crc.h"
#include "server_private.h"
#include <stdlib.h>
#include <string.h>

#include "socket_os.h"

struct socket_link_client {
    struct gracht_server_client base;
    struct sockaddr_storage     address;
    gracht_conn_t               socket;
    gracht_conn_t               link;
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
    struct gracht_buffer* message, unsigned int flags)
{
    unsigned int socketFlags = get_socket_flags(flags);
    intmax_t     bytesWritten;

    GRTRACE(GRSTR("[socket_link_send] sending message"));
    bytesWritten = send(client->base.handle, &message->data[0], message->index, socketFlags);
    if (bytesWritten != message->index) {
        return -1;
    }
    return 0;
}

static int socket_link_recv_client(struct socket_link_client* client,
    struct gracht_message* context, unsigned int flags)
{
    unsigned int socketFlags = get_socket_flags(flags);
    intmax_t     bytesRead;
    uint32_t     missingData;
    
    GRTRACE(GRSTR("socket_link_recv_client reading message header"));
    bytesRead = recv(client->base.handle, &context->payload[0], GRACHT_MESSAGE_HEADER_SIZE, socketFlags);
    if (bytesRead != GRACHT_MESSAGE_HEADER_SIZE) {
        if (bytesRead == 0) {
            errno = (ENODATA);
        }
        return -1;
    }
    
    GRTRACE(GRSTR("socket_link_recv_client message id %u, length of message %u"), 
        *((uint32_t*)&context->payload[0]), *((uint32_t*)&context->payload[4]));
    missingData = *((uint32_t*)&context->payload[4]) - GRACHT_MESSAGE_HEADER_SIZE;
    if (missingData) {
        GRTRACE(GRSTR("socket_link_recv_client reading message payload"));
        bytesRead = recv(client->base.handle, &context->payload[GRACHT_MESSAGE_HEADER_SIZE], 
            (size_t)missingData, MSG_WAITALL);
        if (bytesRead != missingData) {
            // do not process incomplete requests
            GRERROR(GRSTR("socket_link_recv_client did not read full amount of bytes (%u, expected %u)"),
                  (uint32_t)bytesRead, missingData);
            errno = (EPIPE);
            return -1;
        }
    }

    // ->server is set by server
    context->link   = client->link;
    context->client = client->socket;
    context->index  = 0;
    context->size   = *((uint32_t*)&context->payload[4]);
    return 0;
}

static int socket_link_create_client(struct gracht_link_socket* link, struct gracht_message* message,
    struct socket_link_client** clientOut)
{
    struct socket_link_client* client;
    struct sockaddr_storage*   address;
    
    if (!link || !message || !clientOut) {
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
    client->socket      = link->base.connection;

    address = (struct sockaddr_storage*)&message->payload[0];
    memcpy(&client->address, address, (size_t)link->address_length);
    
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

static gracht_conn_t socket_link_setup(struct gracht_link_socket* link)
{
    int status;
    
    if (link->base.type == gracht_link_packet_based) {
        // Create a new socket for listening to events. They are all
        // delivered to fixed sockets on the local system.
        link->base.connection = socket(link->domain, SOCK_DGRAM, 0);
        if (link->base.connection < 0) {
            return GRACHT_CONN_INVALID;
        }
        
        status = bind(link->base.connection,
            (const struct sockaddr*)&link->address, link->address_length);
        if (status) {
            return GRACHT_CONN_INVALID;
        }
        
        return link->base.connection;
    }
    else {
        link->base.connection = socket(link->domain, SOCK_STREAM, 0);
        if (link->base.connection < 0) {
            return GRACHT_CONN_INVALID;
        }
        
        status = bind(link->base.connection,
            (const struct sockaddr*)&link->address, link->address_length);
        if (status) {
            return GRACHT_CONN_INVALID;
        }
        
        // Enable listening for connections, with a maximum of 2 on backlog
        status = listen(link->base.connection, 2);
        if (status) {
            return GRACHT_CONN_INVALID;
        }
        
        return link->base.connection;
    }
    
    errno = (ENOTSUP);
    return GRACHT_CONN_INVALID;
}

static int socket_link_accept(struct gracht_link_socket* link, struct gracht_server_client** clientOut)
{
    struct socket_link_client* client;
    socklen_t                  address_length = link->address_length;
    GRTRACE(GRSTR("[socket_link_accept]"));

    if (link->base.type == gracht_link_packet_based) {
        errno = ENOSYS;
        return -1;
    }
    
    client = (struct socket_link_client*)malloc(sizeof(struct socket_link_client));
    if (!client) {
        GRERROR(GRSTR("link_server: failed to allocate data for link"));
        errno = (ENOMEM);
        return -1;
    }

    memset(client, 0, sizeof(struct socket_link_client));

    client->socket = accept(link->base.connection, (struct sockaddr*)&client->address, &address_length);
    if (client->socket < 0) {
        GRERROR(GRSTR("link_server: failed to accept client: %i - %i"), client->socket, errno);
        free(client);
        return -1;
    }
    client->base.handle = client->socket;
    
    *clientOut = &client->base;
    return 0;
}

static int socket_link_recv_packet(struct gracht_link_socket* link, 
    struct gracht_message* context, unsigned int flags)
{
    socklen_t    addrlen     = link->address_length;
    char*        base        = (char*)&context->payload[addrlen];
    size_t       len         = context->index - addrlen;
    unsigned int socketFlags = get_socket_flags(flags);
    uint32_t     addressCrc;

    if (link->base.type != gracht_link_packet_based) {
        errno = ENOSYS;
        return -1;
    }
    
    // Packets are atomic, either the full packet is there, or none is. So avoid
    // the use of MSG_WAITALL here.
    intmax_t bytesRead = (intmax_t)recvfrom(link->base.connection, base, len, 
        socketFlags, (struct sockaddr*)&context->payload[0], &addrlen);
    if (bytesRead <= 0) {
        if (bytesRead == 0) {
            errno = (ENODATA);
        }
        return -1;
    }

    addressCrc = crc32_generate((const unsigned char*)&context->payload[0], (size_t)addrlen);
    GRTRACE(GRSTR("[gracht_connection_recv_stream] read [%u/%u] addr bytes, %p"),
            addrlen, link->address_length, &context->payload[0]);
    GRTRACE(GRSTR("[gracht_connection_recv_stream] read %lu bytes"), bytesRead);

    // ->server is set by server
    context->link   = link->base.connection;
    context->client = (int)addressCrc;
    context->index  = addrlen;
    context->size   = (uint32_t)bytesRead + (uint32_t)addrlen;
    return 0;
}

static int socket_link_send_packet(struct gracht_link_socket* link,
    struct gracht_message* messageContext, struct gracht_buffer* message)
{
    intmax_t bytesWritten;
    (void)messageContext;

    if (link->base.type != gracht_link_packet_based) {
        errno = ENOSYS;
        return -1;
    }
    
    bytesWritten = send(link->base.connection, &message->data[0], message->index, MSG_WAITALL);
    if (bytesWritten != message->index) {
        GRERROR(GRSTR("link_server: failed to respond [%li/%i]"), bytesWritten, message->index);
        if (bytesWritten == -1) {
            GRERROR(GRSTR("link_server: errno %i"), errno);
        }
        return -1;
    }
    return 0;
}

static void socket_link_destroy(struct gracht_link_socket* link)
{
    if (!link) {
        return;
    }

    if (link->base.connection != GRACHT_CONN_INVALID) {
        close(link->base.connection);
    }
    free(link);
}

void gracht_link_server_socket_api(struct gracht_link_socket* link)
{
    link->base.ops.server.create_client  = (server_create_client_fn)socket_link_create_client;
    link->base.ops.server.destroy_client = (server_destroy_client_fn)socket_link_destroy_client;

    link->base.ops.server.recv_client = (server_recv_client_fn)socket_link_recv_client;
    link->base.ops.server.send_client = (server_send_client_fn)socket_link_send_client;

    link->base.ops.server.setup       = (server_link_setup_fn)socket_link_setup;
    link->base.ops.server.accept      = (server_link_accept_fn)socket_link_accept;
    link->base.ops.server.recv_packet = (server_link_recv_packet_fn)socket_link_recv_packet;
    link->base.ops.server.respond     = (server_link_respond_fn)socket_link_send_packet;
    link->base.ops.server.destroy     = (server_link_destroy_fn)socket_link_destroy;
}
