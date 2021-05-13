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
#include "private.h"
#include <internal/_utils.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>

struct vali_link_client {
    struct gracht_server_client base;
    struct ipmsg_addr           address;
    int                         link;
};

static int vali_link_send_client(struct vali_link_client* client,
    struct gracht_buffer* message, unsigned int flags)
{
    int status;

    status = ipsend(client->link, NULL, message->data, message->index, 0);
    if (status) {
        return status;
    }
    return 0;
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
    
    if (!link || !message || !clientOut) {
        errno = (EINVAL);
        return -1;
    }

    client = (struct vali_link_client*)malloc(sizeof(struct vali_link_client));
    if (!client) {
        errno = (ENOMEM);
        return -1;
    }

    memset(client, 0, sizeof(struct vali_link_client));
    client->base.handle = message->client;
    client->link = link->iod;

    client->address.type = IPMSG_ADDRESS_HANDLE;
    client->address.data.handle = message->client;

    *clientOut = client;
    return 0;
}

static int vali_link_destroy_client(struct vali_link_client* client, gracht_handle_t set_handle)
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

static int vali_link_accept(struct gracht_link_vali* link, gracht_handle_t set_handle, struct gracht_server_client** clientOut)
{
    (void)link;
    (void)set_handle;
    (void)clientOut;
    errno = (ENOTSUP);
    return -1;
}

static int vali_link_recv(struct gracht_link_vali* link, struct gracht_message* context)
{
    UUId_t client;
    int    status;
    
    status = iprecv(link->iod, &context->payload[0], 0, IPMSG_DONTWAIT, &client);
    if (status) {
        return status;
    }

    context->link   = 0;
    context->client = client;
    context->index  = 0;
    context->size   = 0;
    return 0;
}

static int vali_link_send(struct gracht_link_vali* link,
    struct gracht_message* messageContext, struct gracht_buffer* data)
{
    int               status;
    struct ipmsg_addr addr;

    addr.type = IPMSG_ADDRESS_HANDLE;
    addr.data.handle = messageContext->client;

    // send to connection-less client (all of them)
    status = ipsend(link->iod, &addr, data->data, data->index, 0);
    if (status) {
        return status;
    }
    return 0;
}

static void vali_link_destroy(struct gracht_link_vali* link)
{
    if (!link) {
        return;
    }
    
    close(link->iod);
    free(link);
}

static int vali_link_setup(struct gracht_link_vali* link, gracht_handle_t set_handle)
{
    // create an ipc context
    link->iod = ipcontext(0x4000, &link->address); /* 16kB */

    return 0;
}

void gracht_link_server_vali_api(struct gracht_link_vali* link)
{
    link->base.ops.server.accept_client  = (server_accept_client_fn)vali_link_accept;
    link->base.ops.server.create_client  = (server_create_client_fn)vali_link_create_client;
    link->base.ops.server.destroy_client = (server_destroy_client_fn)vali_link_destroy_client;

    link->base.ops.server.recv_client = (server_recv_client_fn)vali_link_recv_client;
    link->base.ops.server.send_client = (server_send_client_fn)vali_link_send_client;

    link->base.ops.server.recv    = (server_link_recv_fn)vali_link_recv;
    link->base.ops.server.send    = (server_link_send_fn)vali_link_send;

    link->base.ops.server.setup   = (server_link_setup_fn)vali_link_setup;
    link->base.ops.server.destroy = (server_link_destroy_fn)vali_link_destroy;
}
