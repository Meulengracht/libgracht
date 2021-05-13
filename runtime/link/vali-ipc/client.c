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
#include <io.h>
#include <stdlib.h>

static int vali_link_connect(struct gracht_link_vali* link)
{
    if (!link) {
        errno = EINVAL;
        return -1;
    }

    // create an ipc context, 16kb should be more than enough
    link->iod = ipcontext(0x4000, NULL);
    if (link->iod < 0) {
        return -1;
    }

    return link->iod;
}

static int vali_link_send(struct gracht_link_vali* link,
                          struct gracht_buffer* message,
                          struct vali_link_message* context)
{
    int status;

    status = ipsend(link->iod, &context->address, message->data, message->index, 0);
    if (status) {
        errno = (EPIPE);
        return GRACHT_MESSAGE_ERROR;
    }
    return GRACHT_MESSAGE_INPROGRESS;
}

static int vali_link_recv(struct gracht_link_vali* link, struct gracht_buffer* message, unsigned int flags)
{
    int status;
    int convertedFlags = 0;

    if (!(flags & GRACHT_MESSAGE_BLOCK)) {
        convertedFlags |= IPMSG_DONTWAIT;
    }

    status = iprecv(link->iod, &message->data[sizeof(UUId_t)], message->index, convertedFlags, (UUId_t*)message->data);
    if (status) {
        return status;
    }

    message->index = sizeof(UUId_t);
    return 0;
}

static void vali_link_destroy(struct gracht_link_vali* link)
{
    if (!link) {
        return;
    }

    if (link->iod > 0) {
        close(link->iod);
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
