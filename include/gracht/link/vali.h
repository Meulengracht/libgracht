/**
 * MollenOS
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

#ifndef __GRACHT_LINK_VALI_H__
#define __GRACHT_LINK_VALI_H__

#include "link.h"
#include "../client.h"
#include <ipcontext.h>
#include <os/osdefs.h>

struct sockaddr_storage;

struct vali_link_message {
    struct gracht_message_context base;
    struct ipmsg_addr             address;
};

struct gracht_link_vali {
    struct gracht_link base;
    int                iod;
};


#define VALI_MSG_INIT_HANDLE(handle) { { 0 }, IPMSG_ADDR_INIT_HANDLE(handle) }

#ifdef __cplusplus
extern "C" {
#endif

// OS API
int gracht_os_get_server_client_address(struct sockaddr_storage*, int*);
int gracht_os_get_server_packet_address(struct sockaddr_storage*, int*);
int gracht_os_thread_set_name(const char*);

// Server API
int gracht_link_vali_server_create(struct gracht_link_vali**, struct ipmsg_addr*);

// Client API
int gracht_link_vali_client_create(struct gracht_link_vali**);

int gracht_vali_message_create(gracht_client_t*, int message_size, struct vali_link_message**);

#ifdef __cplusplus
}
#endif
#endif // !__GRACHT_LINK_VALI_H__
