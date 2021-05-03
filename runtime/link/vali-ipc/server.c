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
 * Gracht Vali Link Type Definitions & Structures
 * - This header describes the base link-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

// Link operations not supported in a packet-based environment
// - Events
// - Stream

#include <errno.h>
#include "gracht/link/vali.h"
#include <internal/_utils.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>

struct vali_link_client {
    struct gracht_server_client base;
    struct ipmsg_addr           address;
};

static int vali_link_send_client(struct vali_link_client* client,
    struct gracht_message* message, unsigned int flags)
{
    struct ipmsg_header ipmsg = {
            .sender  = GetNativeHandle(client->base.iod),
            .address = &client->address,
            .base    = message
    };
    return putmsg(client->base.iod, &ipmsg, 0);
}

static int vali_link_recv_client(struct gracht_server_client* client,
    struct gracht_message* context, unsigned int flags)
{
    errno = (ENOTSUP);
    return -1;
}

static int vali_link_create_client(struct gracht_link_vali* link, struct gracht_message* message,
    struct vali_link_client** clientOut)
{
    struct vali_link_client* client;
    UUId_t                   clientHandle;
    
    if (!link || !message || !clientOut) {
        errno = (EINVAL);
        return -1;
    }

    clientHandle = ((struct ipmsg*)&message->payload[0])->sender;
    client       = (struct vali_link_client*)malloc(sizeof(struct vali_link_client));
    if (!client) {
        errno = (ENOMEM);
        return -1;
    }

    memset(client, 0, sizeof(struct vali_link_client));
    client->base.header.id = message->client;
    client->base.iod = link->iod;

    client->address.type = IPMSG_ADDRESS_HANDLE;
    client->address.data.handle = clientHandle;

    *clientOut = client;
    return 0;
}

static int vali_link_destroy_client(struct vali_link_client* client)
{
    int status;
    
    if (!client) {
        errno = (EINVAL);
        return -1;
    }
    
    status = close(client->base.iod);
    free(client);
    return status;
}

static gracht_conn_t vali_link_listen(struct gracht_link_vali* link, int mode)
{
    if (mode == LINK_LISTEN_DGRAM) {
        return link->iod;
    }
    
    errno = (ENOTSUP);
    return GRACHT_CONN_INVALID;
}

static int vali_link_accept(struct gracht_link_vali* link, struct gracht_server_client** clientOut)
{
    errno = (ENOTSUP);
    return -1;
}

static int vali_link_recv_packet(struct gracht_link_vali* link, struct gracht_message* context)
{
    struct ipmsg* message = (struct ipmsg*)&context->payload[0];
    int           status;
    
    status = getmsg(link->iod, message, GRACHT_DEFAULT_MESSAGE_SIZE, IPMSG_DONTWAIT);
    if (status) {
        return status;
    }

    context->message_id  = message->base.header.id;
    context->client      = (int)message->sender;
    context->params      = &message->base.params[0];
    
    context->param_in    = message->base.header.param_in;
    context->param_count = message->base.header.param_in + message->base.header.param_out;
    context->protocol    = message->base.header.protocol;
    context->action      = message->base.header.action;
    return 0;
}

static int vali_link_respond(struct gracht_link_vali* link,
    struct gracht_message* messageContext, struct gracht_message* message)
{
    struct ipmsg* recvmsg = (struct ipmsg*)&messageContext->payload[0];
    struct ipmsg_addr ipaddr = {
            .type = IPMSG_ADDRESS_HANDLE,
            .data.handle = recvmsg->sender
    };
    struct ipmsg_header ipmsg = {
            .sender  = GetNativeHandle(link->iod),
            .address = &ipaddr,
            .base    = message
    };
    return resp(link->iod, &messageContext->payload[0], &ipmsg);
}

static void vali_link_destroy(struct gracht_link_vali* link)
{
    if (!link) {
        return;
    }
    
    close(link->iod);
    free(link);
}

int gracht_link_vali_server_create(struct gracht_link_vali** linkOut, struct ipmsg_addr* address)
{
    struct gracht_link_vali* link;
    
    link = (struct gracht_link_vali*)malloc(sizeof(struct gracht_link_vali));
    if (!link) {
        errno = (ENOMEM);
        return -1;
    }
    
    // create an ipc context
    link->iod = ipcontext(0x4000, address); /* 16kB */
    
    // initialize link operations    
    link->ops.create_client  = (server_create_client_fn)vali_link_create_client;
    link->ops.destroy_client = (server_destroy_client_fn)vali_link_destroy_client;

    link->ops.recv_client = (server_recv_client_fn)vali_link_recv_client;
    link->ops.send_client = (server_send_client_fn)vali_link_send_client;

    link->ops.listen      = (server_link_listen_fn)vali_link_listen;
    link->ops.accept      = (server_link_accept_fn)vali_link_accept;
    link->ops.recv_packet = (server_link_recv_packet_fn)vali_link_recv_packet;
    link->ops.respond     = (server_link_respond_fn)vali_link_respond;
    link->ops.destroy     = (server_link_destroy_fn)vali_link_destroy;
    
    *linkOut = &link->ops;
    return 0;
}
