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
#include <stdlib.h>
#include <string.h>

#include "socket_os.h"

struct socket_link_manager {
    struct client_link_ops             ops;
    struct socket_client_configuration config;
    gracht_conn_t                      iod;
};

static int socket_link_send_stream(struct socket_link_manager* linkManager,
    struct gracht_buffer* message)
{
    intmax_t byteCount;
    
    byteCount = send(linkManager->iod, &message->data[0], message->index, 0);
    if (byteCount != message->index) {
        GRERROR(GRSTR("link_client: failed to send message, bytes sent: %li, expected: %u (%i)"),
              byteCount, message->index, errno);
        errno = (EPIPE);
        return GRACHT_MESSAGE_ERROR;
    }
    return GRACHT_MESSAGE_INPROGRESS;
}

static int socket_link_recv_stream(struct socket_link_manager* linkManager,
    struct gracht_buffer* message, unsigned int flags)
{
    size_t   bytesRead;
    uint32_t missingData;
    
    GRTRACE(GRSTR("[gracht_connection_recv_stream] reading message header"));
    bytesRead = recv(linkManager->iod, &message->data[0], GRACHT_MESSAGE_HEADER_SIZE, flags);
    if (bytesRead != GRACHT_MESSAGE_HEADER_SIZE) {
        if (bytesRead == 0) {
            errno = (ENODATA);
        }
        else {
            errno = (EPIPE);
        }
        return -1;
    }
    
    missingData = *((uint32_t*)&message->data[4]) - GRACHT_MESSAGE_HEADER_SIZE;
    if (missingData) {
        GRTRACE(GRSTR("[gracht_connection_recv_stream] reading message payload"));
        bytesRead = recv(linkManager->iod, &message->data[GRACHT_MESSAGE_HEADER_SIZE], missingData, MSG_WAITALL);
        if (bytesRead != missingData) {
            // do not process incomplete requests
            GRERROR(GRSTR("[gracht_connection_recv_message] did not read full amount of bytes (%u, expected %u)"),
                  (uint32_t)bytesRead, missingData);
            errno = (EPIPE);
            return -1; 
        }
    }

    message->index = 0;
    return 0;
}

static int socket_link_send_packet(struct socket_link_manager* linkManager, struct gracht_buffer* message)
{
    intmax_t byteCount;

    GRTRACE(GRSTR("link_client: send message (%u)"), message->index);

    byteCount = send(linkManager->iod, &message->data[0], message->index, 0);
    if (byteCount != message->index) {
        GRERROR(GRSTR("link_client: failed to send message, bytes sent: %u, expected: %u"),
              (uint32_t)byteCount, message->index);
        errno = (EPIPE);
        return GRACHT_MESSAGE_ERROR;
    }
    return GRACHT_MESSAGE_INPROGRESS;
}

static int socket_link_recv_packet(struct socket_link_manager* linkManager, struct gracht_buffer* message, unsigned int flags)
{
    socklen_t addrlen = linkManager->config.address_length;
    char*     base    = &message->data[addrlen];
    size_t    len     = message->index - addrlen;
    
    message->index = addrlen;
    
    // Packets are atomic, either the full packet is there, or none is. So avoid
    // the use of MSG_WAITALL here.
    GRTRACE(GRSTR("[gracht_connection_recv_stream] reading full message"));
    intmax_t bytes_read = (intmax_t)recvfrom(linkManager->iod, base, len, flags, 
        (struct sockaddr*)&message->data[0], &addrlen);
    if (bytes_read < GRACHT_MESSAGE_HEADER_SIZE) {
        if (bytes_read == 0) {
            errno = (ENODATA);
        }
        else {
            errno = (EPIPE);
        }
        return -1;
    }
    return 0;
}

static gracht_conn_t socket_link_connect(struct socket_link_manager* linkManager)
{
    int type = linkManager->config.type == gracht_link_stream_based ? SOCK_STREAM : SOCK_DGRAM;
    
    linkManager->iod = socket(AF_LOCAL, type, 0);
    if (linkManager->iod < 0) {
        GRERROR(GRSTR("client_link: failed to create socket"));
        return -1;
    }
    
    int status = connect(linkManager->iod, 
        (const struct sockaddr*)&linkManager->config.address,
        linkManager->config.address_length);
    if (status) {
        GRERROR(GRSTR("client_link: failed to connect to socket"));
        close(linkManager->iod);
        return status;
    }
    return linkManager->iod;
}

static int socket_link_recv(struct socket_link_manager* linkManager,
    struct gracht_buffer* message, unsigned int flags)
{
    unsigned int convertedFlags = MSG_WAITALL;
    
    if (!(flags & GRACHT_MESSAGE_BLOCK)) {
        convertedFlags |= MSG_DONTWAIT;
    }
    
    if (linkManager->config.type == gracht_link_stream_based) {
        return socket_link_recv_stream(linkManager, message, convertedFlags);
    }
    else if (linkManager->config.type == gracht_link_packet_based) {
        return socket_link_recv_packet(linkManager, message, convertedFlags);
    }
    
    errno = (ENOTSUP);
    return -1;
}

static int socket_link_send(struct socket_link_manager* linkManager,
    struct gracht_buffer* message, void* messageContext)
{
    // not used for socket
    (void)messageContext;

    if (linkManager->config.type == gracht_link_stream_based) {
        return socket_link_send_stream(linkManager, message);
    }
    else if (linkManager->config.type == gracht_link_packet_based) {
        return socket_link_send_packet(linkManager, message);
    }
    else
    {
        errno = (ENOTSUP);
        return GRACHT_MESSAGE_ERROR;
    }
}

static void socket_link_destroy(struct socket_link_manager* linkManager)
{
    if (!linkManager) {
        return;
    }
    
    if (linkManager->iod > 0) {
        close(linkManager->iod);
    }
    
    free(linkManager);
}

int gracht_link_socket_client_create(struct client_link_ops** linkOut, 
    struct socket_client_configuration* configuration)
{
    struct socket_link_manager* linkManager;
    
    linkManager = (struct socket_link_manager*)malloc(sizeof(struct socket_link_manager));
    if (!linkManager) {
        errno = (ENOMEM);
        return -1;
    }
    
    memset(linkManager, 0, sizeof(struct socket_link_manager));
    memcpy(&linkManager->config, configuration, sizeof(struct socket_client_configuration));

    linkManager->ops.connect     = (client_link_connect_fn)socket_link_connect;
    linkManager->ops.recv        = (client_link_recv_fn)socket_link_recv;
    linkManager->ops.send        = (client_link_send_fn)socket_link_send;
    linkManager->ops.destroy     = (client_link_destroy_fn)socket_link_destroy;
    
    *linkOut = &linkManager->ops;
    return 0;
}
