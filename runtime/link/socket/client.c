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
 * Gracht Socket Link Type Definitions & Structures
 * - This header describes the base link-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <assert.h>
#include <errno.h>
#include "gracht/link/socket.h"
#include "logging.h"
#include <stdlib.h>
#include <string.h>

#include "socket_os.h"

static int socket_link_send_stream(struct gracht_link_socket* link,
    struct gracht_buffer* message)
{
    long byteCount;
    
    byteCount = (long)send(link->base.connection, &message->data[0], message->index, 0);
    if ((uint32_t)byteCount != message->index) {
        GRERROR(GRSTR("link_client: failed to send message, bytes sent: %li, expected: %u (%i)"),
              byteCount, message->index, errno);
        errno = (EPIPE);
        return -1;
    }
    return 0;
}

static int socket_link_recv_stream(struct gracht_link_socket* link,
    struct gracht_buffer* message, unsigned int flags)
{
    size_t   bytesRead;
    uint32_t missingData;
    
    GRTRACE(GRSTR("[gracht_connection_recv_stream] reading message header"));
    bytesRead = recv(link->base.connection, &message->data[0], GRACHT_MESSAGE_HEADER_SIZE, flags);
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
        bytesRead = recv(link->base.connection, &message->data[GRACHT_MESSAGE_HEADER_SIZE], missingData, MSG_WAITALL);
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

static int socket_link_send_packet(struct gracht_link_socket* link, struct gracht_buffer* message)
{
    intmax_t byteCount;

    GRTRACE(GRSTR("link_client: send message (%u)"), message->index);

    byteCount = send(link->base.connection, &message->data[0], message->index, 0);
    if (byteCount != message->index) {
        GRERROR(GRSTR("link_client: failed to send message, bytes sent: %u, expected: %u"),
              (uint32_t)byteCount, message->index);
        errno = (EPIPE);
        return -1;
    }
    return 0;
}

static int socket_link_recv_packet(struct gracht_link_socket* link, struct gracht_buffer* message, unsigned int flags)
{
    // important to use the address we receive from (i.e 
    // the length of the address we connect to)
    socklen_t addrlen = link->connect_address_length;
    char*     base    = &message->data[addrlen];
    size_t    len     = message->index - addrlen;
    long      bytes_read;
    
    message->index = addrlen;
    
    // Packets are atomic, either the full packet is there, or none is. So avoid
    // the use of MSG_WAITALL here.
    GRTRACE(GRSTR("[gracht_connection_recv_stream] reading full message"));
    bytes_read = (long)recvfrom(link->base.connection, base, len, flags, 
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

static gracht_conn_t socket_link_connect(struct gracht_link_socket* link)
{
    int type = link->base.type == gracht_link_stream_based ? SOCK_STREAM : SOCK_DGRAM;
    int status;
    
    link->base.connection = socket(link->domain, type, 0);
    if (link->base.connection < 0) {
        GRERROR(GRSTR("client_link: failed to create socket"));
        return -1;
    }
    
    if (link->bind_address_length > 0) {
        status = bind(link->base.connection, 
            (const struct sockaddr*)&link->bind_address, 
            link->bind_address_length);
        if (status) {
            GRERROR(GRSTR("client_link: failed to bind socket to local address"));
            close(link->base.connection);
            link->base.connection = GRACHT_CONN_INVALID;
            return status;
        }
    }

    status = connect(link->base.connection, 
        (const struct sockaddr*)&link->connect_address,
        link->connect_address_length);
    if (status) {
        GRERROR(GRSTR("client_link: failed to connect to socket"));
        close(link->base.connection);
        link->base.connection = GRACHT_CONN_INVALID;
        return status;
    }
    return link->base.connection;
}

static int socket_link_recv(struct gracht_link_socket* link,
    struct gracht_buffer* message, unsigned int flags)
{
    unsigned int convertedFlags = MSG_WAITALL;
    int          status         = -1;

#ifdef _WIN32
        __set_nonblocking_if_needed(link->base.connection, flags);
#endif

    if (!(flags & GRACHT_MESSAGE_BLOCK)) {
        convertedFlags |= MSG_DONTWAIT;
    }
    
    if (link->base.type == gracht_link_stream_based) {
        status = socket_link_recv_stream(link, message, convertedFlags);
    }
    else if (link->base.type == gracht_link_packet_based) {
        status = socket_link_recv_packet(link, message, convertedFlags);
    }
    else {
        errno = (ENOTSUP);
    }
    return status;
}

static int socket_link_send(struct gracht_link_socket* link,
    struct gracht_buffer* message, void* messageContext)
{
    // not used for socket
    (void)messageContext;

#ifdef _WIN32
        __set_nonblocking_if_needed(link->base.connection, GRACHT_MESSAGE_BLOCK);
#endif

    if (link->base.type == gracht_link_stream_based) {
        return socket_link_send_stream(link, message);
    }
    else if (link->base.type == gracht_link_packet_based) {
        return socket_link_send_packet(link, message);
    }
    else
    {
        errno = (ENOTSUP);
        return GRACHT_MESSAGE_ERROR;
    }
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

void gracht_link_client_socket_api(struct gracht_link_socket* link)
{
    link->base.ops.client.connect = (client_link_connect_fn)socket_link_connect;
    link->base.ops.client.recv    = (client_link_recv_fn)socket_link_recv;
    link->base.ops.client.send    = (client_link_send_fn)socket_link_send;
    link->base.ops.client.destroy = (client_link_destroy_fn)socket_link_destroy;
}
