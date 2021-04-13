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
#include "../include/gracht/link/socket.h"
#include "../include/debug.h"
#include <stdlib.h>
#include <string.h>

#include "socket_os.h"

struct socket_link_manager {
    struct client_link_ops             ops;
    struct socket_client_configuration config;
    gracht_conn_t                      iod;
};

static int socket_link_send_stream(struct socket_link_manager* linkManager,
    struct gracht_message* message)
{
    i_iobuf_t*    iov = alloca(sizeof(i_iobuf_t) * (1 + message->header.param_in));
    uint32_t      i;
    int           iovCount = 1;
    intmax_t      byteCount;
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

    byteCount = sendmsg(linkManager->iod, &msg, 0);
    if (byteCount != message->header.length) {
        GRERROR(GRSTR("link_client: failed to send message, bytes sent: %li, expected: %u (%i)"),
              byteCount, message->header.length, errno);
        errno = (EPIPE);
        return GRACHT_MESSAGE_ERROR;
    }

    return GRACHT_MESSAGE_INPROGRESS;
}

static int socket_link_recv_stream(struct socket_link_manager* linkManager,
    void* messageBuffer, unsigned int flags, struct gracht_message** messageOut)
{
    struct gracht_message* message = messageBuffer;
    char*                  params_storage;
    size_t                 bytes_read;
    
    GRTRACE(GRSTR("[gracht_connection_recv_stream] reading message header"));
    bytes_read = recv(linkManager->iod, message, sizeof(struct gracht_message), flags);
    if (bytes_read != sizeof(struct gracht_message)) {
        if (bytes_read == 0) {
            errno = (ENODATA);
        }
        else {
            errno = (EPIPE);
        }
        return -1;
    }
    
    if (message->header.param_in) {
        GRTRACE(GRSTR("[gracht_connection_recv_stream] reading message payload"));
        
        params_storage = (char*)messageBuffer + sizeof(struct gracht_message);
        bytes_read     = recv(linkManager->iod, params_storage, message->header.length - sizeof(struct gracht_message), MSG_WAITALL);
        if (bytes_read != message->header.length - sizeof(struct gracht_message)) {
            // do not process incomplete requests
            GRERROR(GRSTR("[gracht_connection_recv_message] did not read full amount of bytes (%u, expected %u)"),
                  (uint32_t)bytes_read, (uint32_t)(message->header.length - sizeof(struct gracht_message)));
            errno = (EPIPE);
            return -1; 
        }
    }
    
    *messageOut = message;
    return 0;
}

static int socket_link_send_packet(struct socket_link_manager* linkManager, struct gracht_message* message)
{
    i_iobuf_t*    iov = alloca(sizeof(i_iobuf_t) * (1 + message->header.param_in));
    uint32_t      i;
    int           iovCount = 1;
    intmax_t      byteCount;
    i_msghdr_t    msg = I_MSGHDR_INIT;

    GRTRACE(GRSTR("link_client: send message (%u, in %i, out %i)"),
          message->header.length, message->header.param_in, message->header.param_out);

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
    
    byteCount = sendmsg(linkManager->iod, &msg, 0);
    if (byteCount != message->header.length) {
        GRERROR(GRSTR("link_client: failed to send message, bytes sent: %u, expected: %u"),
              (uint32_t)byteCount, message->header.length);
        errno = (EPIPE);
        return GRACHT_MESSAGE_ERROR;
    }

    return GRACHT_MESSAGE_INPROGRESS;
}

static int socket_link_recv_packet(struct socket_link_manager* linkManager, 
    void* messageBuffer, unsigned int flags, struct gracht_message** messageOut)
{
    struct gracht_message* message = (struct gracht_message*)((char*)messageBuffer + linkManager->config.address_length);
    i_msghdr_t             msg = I_MSGHDR_INIT;
    i_iobuf_t              iov[1];
    
    i_iobuf_set_buf(&iov[0], message);
    i_iobuf_set_len(&iov[0], GRACHT_DEFAULT_MESSAGE_SIZE);
    
    i_msghdr_set_addr(&msg, messageBuffer, linkManager->config.address_length);
    i_msghdr_set_bufs(&msg, &iov[0], 1);
    
    // Packets are atomic, either the full packet is there, or none is. So avoid
    // the use of MSG_WAITALL here.
    GRTRACE(GRSTR("[gracht_connection_recv_stream] reading full message"));
    intmax_t bytes_read = recvmsg(linkManager->iod, &msg, flags);
    if (bytes_read < (intmax_t)sizeof(struct gracht_message)) {
        if (bytes_read == 0) {
            errno = (ENODATA);
        }
        else {
            errno = (EPIPE);
        }
        return -1;
    }
    
    *messageOut = message;
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
    void* messageBuffer, unsigned int flags, struct gracht_message** messageOut)
{
    unsigned int convertedFlags = MSG_WAITALL;
    
    if (!(flags & GRACHT_MESSAGE_BLOCK)) {
        convertedFlags |= MSG_DONTWAIT;
    }
    
    if (linkManager->config.type == gracht_link_stream_based) {
        return socket_link_recv_stream(linkManager, messageBuffer, convertedFlags, messageOut);
    }
    else if (linkManager->config.type == gracht_link_packet_based) {
        return socket_link_recv_packet(linkManager, messageBuffer, convertedFlags, messageOut);
    }
    
    errno = (ENOTSUP);
    return -1;
}

static int socket_link_send(struct socket_link_manager* linkManager,
    struct gracht_message* message, void* messageContext)
{
    // not used for socket
    (void)messageContext;

    // perform length check before sending
    if (message->header.length > GRACHT_DEFAULT_MESSAGE_SIZE) {
        errno = (E2BIG);
        return GRACHT_MESSAGE_ERROR;
    }
    
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
