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
    int                         streaming;
#ifdef _WIN32
    WSABUF                      waitbuf;
    uint8_t                     headerbuf[GRACHT_MESSAGE_HEADER_SIZE];
    DWORD                       flags;
    WSAOVERLAPPED               overlapped;
#endif
};

#ifdef _WIN32
static int queue_accept(struct gracht_link_socket* link, gracht_handle_t iocp_handle)
{
    struct socket_link_client* client;
    BOOL                       status;
    GRTRACE(GRSTR("queue_accept"));

    client = malloc(sizeof(struct socket_link_client));
    if (!client) {
        errno = ENOMEM;
        return -1;
    }
    memset(client, 0, sizeof(struct socket_link_client));

    client->socket = WSASocket(link->domain, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (client->socket == INVALID_SOCKET) {
        free(client);
        errno = ENODEV;
        return -1;
    }

    client->base.handle = client->socket;
    client->streaming   = 1;
    client->waitbuf.buf = &client->headerbuf[0];
    client->waitbuf.len = GRACHT_MESSAGE_HEADER_SIZE;

    status = AcceptEx(link->base.connection, client->socket, &link->buffer[0], 0, 
        link->address_length + 16, link->address_length + 16,
        NULL, &client->overlapped);
    if (!status) {
        DWORD reason = WSAGetLastError();
        if (reason != WSA_IO_PENDING) {
            closesocket(client->socket);
            free(client);
            return -1;
        }
    }
    link->pending = client;
    return 0;
}

#endif

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
#ifdef _WIN32
    DWORD overlappedFlags;
    DWORD overlappedLength;
    BOOL  status;

    // extract the number of bytes received
    status = WSAGetOverlappedResult(client->socket, &client->overlapped, &overlappedLength, FALSE, &overlappedFlags);
    if (status == FALSE) {
        errno = ENODATA;
        return -1;
    }
    
    // detect disconnections
    if (!overlappedLength) {
        errno = EFAULT;
        return -1;
    }

    memcpy(&context->payload[0], &client->headerbuf[0], overlappedLength);
    if (overlappedLength != GRACHT_MESSAGE_HEADER_SIZE) {
        GRTRACE(GRSTR("socket_link_recv_client reading rest of message header %li/%i"), overlappedLength, GRACHT_MESSAGE_HEADER_SIZE);
        missingData = GRACHT_MESSAGE_HEADER_SIZE - overlappedLength;
        bytesRead   = recv(client->base.handle, &context->payload[overlappedLength], missingData, MSG_WAITALL);
        if (bytesRead != missingData) {
            return -1;
        }
    }
#else
    bytesRead = recv(client->base.handle, &context->payload[0], GRACHT_MESSAGE_HEADER_SIZE, socketFlags);
    if (bytesRead != GRACHT_MESSAGE_HEADER_SIZE) {
        if (bytesRead == 0) {
            errno = ENODATA;
        }
        return -1;
    }
#endif
    
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

#ifdef _WIN32
    // queue up another read
    status = WSARecv(client->socket, &client->waitbuf, 1, NULL, &client->flags, &client->overlapped, NULL);
    if (status == SOCKET_ERROR) {
        DWORD reason = WSAGetLastError();
        if (reason != WSA_IO_PENDING) {
            GRERROR(GRSTR("socket_link_recv_client failed to queue up a read on the client socket: %u"), reason);
        }
    }
#endif
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
    client->streaming   = 0;

    address = (struct sockaddr_storage*)&message->payload[0];
    memcpy(&client->address, address, (size_t)link->address_length);
    
    *clientOut = client;
    return 0;
}

static int socket_link_destroy_client(struct socket_link_client* client, gracht_handle_t set_handle)
{
    int status;
    
    if (!client) {
        errno = (EINVAL);
        return -1;
    }
    
    // remove the client if the client is a streaming one
    if (client->streaming) {
        status = socket_aio_remove(set_handle, client->socket);
        if (status) {
            GRWARNING(GRSTR("socket_link_destroy_client failed to remove client socket from set_handle"));
        }
    }
    status = close(client->base.handle);
    free(client);
    return status;
}

static gracht_conn_t socket_link_setup(struct gracht_link_socket* link, gracht_handle_t set_handle)
{
    int status;
    
    if (link->base.type == gracht_link_packet_based) {
        // Create a new socket for listening to events. They are all
        // delivered to fixed sockets on the local system.
#ifdef _WIN32
        link->base.connection = WSASocket(link->domain, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, WSA_FLAG_OVERLAPPED);
#else
        link->base.connection = socket(link->domain, SOCK_DGRAM, 0);
#endif
        if (link->base.connection == GRACHT_CONN_INVALID) {
            return GRACHT_CONN_INVALID;
        }

        status = bind(link->base.connection,
            (const struct sockaddr*)&link->address, link->address_length);
        if (status) {
            return GRACHT_CONN_INVALID;
        }
        
        status = socket_aio_add(set_handle, link->base.connection);
        if (status) {
            GRWARNING(GRSTR("socket_link_setup failed to add socket to set_handle"));
        }

#ifdef _WIN32
        // initialize the waitbuf
        link->waitbuf.buf = &link->buffer[0];
        link->waitbuf.len = GRACHT_MESSAGE_HEADER_SIZE;
        link->recvLength  = (int)link->address_length;

        // queue up the first read
        status = WSARecvFrom(link->base.connection, &link->waitbuf, 1, NULL, &link->recvFlags,
            (sockaddr*)&link->buffer[GRACHT_MESSAGE_HEADER_SIZE], &link->recvLength, &link->overlapped, NULL);
        if (status == SOCKET_ERROR) {
            DWORD reason = WSAGetLastError();
            if (reason != WSA_IO_PENDING) {
                GRERROR(GRSTR("socket_link_setup failed to queue up a read on the client socket: %u"), reason);
            }
        }
#endif
        return link->base.connection;
    }
    else {
#ifdef _WIN32
        link->base.connection = WSASocket(link->domain, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
#else
        link->base.connection = socket(link->domain, SOCK_STREAM, 0);
#endif
        if (link->base.connection == GRACHT_CONN_INVALID) {
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
        
        status = socket_aio_add(set_handle, link->base.connection);
        if (status) {
            GRWARNING(GRSTR("socket_link_setup failed to add socket to set_handle"));
        }

#ifdef _WIN32
        status = queue_accept(link, set_handle);
        if (status) {
            GRERROR(GRSTR("socket_link_setup failed to queue up an accept on the listen socket"));
        }
#endif
        return link->base.connection;
    }
    
    errno = (ENOTSUP);
    return GRACHT_CONN_INVALID;
}

#ifdef _WIN32
static int socket_link_accept(
    struct gracht_link_socket*    link,
    gracht_handle_t               iocp_handle,
    struct gracht_server_client** clientOut)
{
    struct socket_link_client* client         = link->pending;
    socklen_t                  address_length = link->address_length;
    struct sockaddr*           remote         = NULL;
    int                        remote_length  = 0;
    struct sockaddr*           local          = NULL;
    int                        local_length   = 0;
    int                        status;
    GRTRACE(GRSTR("socket_link_accept"));

    if (link->base.type == gracht_link_packet_based) {
        errno = ENOSYS;
        return -1;
    }

    // extract the client address from the link buffer
    GetAcceptExSockaddrs(&link->buffer[0], 0, address_length + 16, address_length + 16,
        &local, &local_length, &remote, &remote_length);
    memcpy(&client->address, remote, remote_length);

    // add the new socket to the iocp
    status = socket_aio_add(iocp_handle, client->socket);
    if (status) {
        GRWARNING(GRSTR("socket_link_accept failed to add socket to set_handle"));
    }

    // queue up yet another accept
    status = queue_accept(link, iocp_handle);
    if (status) {
        GRERROR(GRSTR("socket_link_accept failed to queue up an accept on the listen socket"));
    }

    // queue up a read on the new client
    status = WSARecv(client->socket, &client->waitbuf, 1, NULL, &client->flags, &client->overlapped, NULL);
    if (status == SOCKET_ERROR) {
        DWORD reason = WSAGetLastError();
        if (reason != WSA_IO_PENDING) {
            GRERROR(GRSTR("socket_link_accept failed to queue up a read on the client socket: %u"), reason);
        }
    }

    *clientOut = &client->base;
    return 0;
}
#else
static int socket_link_accept(
    struct gracht_link_socket*    link,
    gracht_handle_t               set_handle,
    struct gracht_server_client** clientOut)
{
    struct socket_link_client* client;
    socklen_t                  address_length = link->address_length;
    int                        status;
    GRTRACE(GRSTR("socket_link_accept"));

    if (link->base.type == gracht_link_packet_based) {
        errno = ENOSYS;
        return -1;
    }
    
    client = (struct socket_link_client*)malloc(sizeof(struct socket_link_client));
    if (!client) {
        GRERROR(GRSTR("socket_link_accept failed to allocate data for link"));
        errno = (ENOMEM);
        return -1;
    }

    memset(client, 0, sizeof(struct socket_link_client));

    client->socket = accept(link->base.connection, (struct sockaddr*)&client->address, &address_length);
    if (client->socket < 0) {
        GRERROR(GRSTR("socket_link_accept failed to accept client: %i - %i"), client->socket, errno);
        free(client);
        return -1;
    }
    client->base.handle = client->socket;
    client->streaming   = 1;
    
    status = socket_aio_add(set_handle, client->socket);
    if (status) {
        GRWARNING(GRSTR("socket_link_accept failed to add socket to set_handle"));
    }
    
    *clientOut = &client->base;
    return 0;
}
#endif

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

#ifdef _WIN32
    uint32_t restOfData;
    DWORD    overlappedFlags;
    DWORD    overlappedLength;
    BOOL     didSucceed;
    int      bytesRead;

    // extract the number of bytes received
    didSucceed = WSAGetOverlappedResult(link->base.connection, &link->overlapped, &overlappedLength, FALSE, &overlappedFlags);
    if (didSucceed == FALSE || !overlappedLength) {
        errno = ENODATA;
        return -1;
    }

    memcpy(&context->payload[0], &link->buffer[0], overlappedLength);
    if (overlappedLength < GRACHT_MESSAGE_HEADER_SIZE) {
        GRTRACE(GRSTR("socket_link_recv_client reading rest of message header %li/%i"), overlappedLength, GRACHT_MESSAGE_HEADER_SIZE);
        restOfData = GRACHT_MESSAGE_HEADER_SIZE - overlappedLength;
        bytesRead = recv(link->base.connection, &context->payload[overlappedLength], restOfData, MSG_WAITALL);
        if (bytesRead != restOfData) {
            return -1;
        }
    }
#else
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
#endif

    addressCrc = crc32_generate((const unsigned char*)&context->payload[0], (size_t)addrlen);
    GRTRACE(GRSTR("socket_link_recv_packet read [%u/%u] addr bytes, %p"),
            addrlen, link->address_length, &context->payload[0]);
    GRTRACE(GRSTR("socket_link_recv_packet read %lu bytes"), bytesRead);

    // ->server is set by server
    context->link   = link->base.connection;
    context->client = (int)addressCrc;
    context->index  = addrlen;
    context->size   = (uint32_t)bytesRead + (uint32_t)addrlen;

#ifdef _WIN32
    // queue up another read
    int status = WSARecvFrom(link->base.connection, &link->waitbuf, 1, NULL, &link->recvFlags,
        (sockaddr*)&link->buffer[GRACHT_MESSAGE_HEADER_SIZE], &link->recvLength, &link->overlapped, NULL);
    if (status == SOCKET_ERROR) {
        DWORD reason = WSAGetLastError();
        if (reason != WSA_IO_PENDING) {
            GRERROR(GRSTR("socket_link_recv_packet failed to queue up a read on the client socket: %u"), reason);
        }
    }
#endif
    return 0;
}

static int socket_link_send_packet(struct gracht_link_socket* link,
    struct gracht_message* messageContext, struct gracht_buffer* message)
{
    long bytesWritten;
    (void)messageContext;

    if (link->base.type != gracht_link_packet_based) {
        errno = ENOSYS;
        return -1;
    }
    
    bytesWritten = (long)send(link->base.connection, &message->data[0], message->index, MSG_WAITALL);
    if (bytesWritten != message->index) {
        GRERROR(GRSTR("link_server: failed to respond [%li/%i]"), bytesWritten, message->index);
        if (bytesWritten == -1) {
            GRERROR(GRSTR("link_server: errno %i"), errno);
        }
        return -1;
    }
    return 0;
}

static void socket_link_destroy(struct gracht_link_socket* link, gracht_handle_t set_handle)
{
    if (!link) {
        return;
    }

    if (link->base.connection != GRACHT_CONN_INVALID) {
        int status = socket_aio_remove(set_handle, link->base.connection);
        if (status) {
            GRWARNING(GRSTR("socket_link_destroy failed to remove link socket from set_handle"));
        }

        close(link->base.connection);
    }
    free(link);
}

void gracht_link_server_socket_api(struct gracht_link_socket* link)
{
    link->base.ops.server.accept_client  = (server_accept_client_fn)socket_link_accept;
    link->base.ops.server.create_client  = (server_create_client_fn)socket_link_create_client;
    link->base.ops.server.destroy_client = (server_destroy_client_fn)socket_link_destroy_client;

    link->base.ops.server.recv_client = (server_recv_client_fn)socket_link_recv_client;
    link->base.ops.server.send_client = (server_send_client_fn)socket_link_send_client;

    link->base.ops.server.recv    = (server_link_recv_fn)socket_link_recv_packet;
    link->base.ops.server.send    = (server_link_send_fn)socket_link_send_packet;

    link->base.ops.server.setup   = (server_link_setup_fn)socket_link_setup;
    link->base.ops.server.destroy = (server_link_destroy_fn)socket_link_destroy;
}
