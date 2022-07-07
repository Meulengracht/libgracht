/**
 * Vali
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
#include <io.h>
#include <stdlib.h>

static int vali_link_connect(struct gracht_link_vali* link)
{
    if (!link || link->base.type != gracht_link_packet_based) {
        errno = EINVAL;
        return -1;
    }

    // create an ipc context, 16kb should be more than enough
    link->base.connection = ipcontext(0x4000, NULL);
    if (link->base.connection == GRACHT_CONN_INVALID) {
        return -1;
    }

    return link->base.connection;
}

static int vali_link_send(struct gracht_link_vali* link,
                          struct gracht_buffer* message,
                          struct vali_link_message* context)
{
    int status;

    status = ipsend(link->base.connection, &context->address, message->data, message->index, 0);
    if (status) {
        errno = (EPIPE);
        return GRACHT_MESSAGE_ERROR;
    }
    return GRACHT_MESSAGE_INPROGRESS;
}

static inline int get_ip_flags(unsigned int flags)
{
    int ipFlags = 0;
    if (!(flags & GRACHT_MESSAGE_BLOCK)) {
        ipFlags |= IPMSG_DONTWAIT;
    }
    return ipFlags;
}

static int vali_link_recv(struct gracht_link_vali* link, struct gracht_buffer* message, unsigned int flags)
{
    int bytesRead;
    int ipFlags = get_ip_flags(flags);
    int index   = sizeof(uuid_t);

    bytesRead = iprecv(link->base.connection, &message->data[index], message->index, ipFlags, (uuid_t*)message->data);
    if (bytesRead < 0) {
        return bytesRead;
    }

    message->index = index;
    return 0;
}

static void vali_link_destroy(struct gracht_link_vali* link)
{
    if (!link) {
        return;
    }

    if (link->base.connection != GRACHT_CONN_INVALID) {
        close(link->base.connection);
    }
    free(link);
}

void gracht_link_client_vali_api(struct gracht_link_vali* link)
{
    link->base.ops.client.connect = (client_link_connect_fn)vali_link_connect;
    link->base.ops.client.recv    = (client_link_recv_fn)vali_link_recv;
    link->base.ops.client.send    = (client_link_send_fn)vali_link_send;
    link->base.ops.client.destroy = (client_link_destroy_fn)vali_link_destroy;
}
