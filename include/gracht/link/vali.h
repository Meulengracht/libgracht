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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
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
#include <os/types/ipc.h>
#include <os/osdefs.h>

struct vali_link_message {
    struct gracht_message_context base;
    IPCAddress_t                  address;
};

#define VALI_MSG_INIT_HANDLE(handle) { { 0 }, IPC_ADDRESS_HANDLE_INIT(handle) }

#ifdef __cplusplus
extern "C" {
#endif


/**
 * Represents the vali link datastructure, and can be configured to work
 * however wanted. The default configuration is non-listen, connection-less mode.
 * The address must be provided as it either provides the address it should listen on or
 * the address it should connect to
 */
struct gracht_link_vali;

GRACHTAPI int  gracht_link_vali_create(struct gracht_link_vali** linkOut);
GRACHTAPI void gracht_link_vali_set_listen(struct gracht_link_vali* link, int listen);
GRACHTAPI void gracht_link_vali_set_address(struct gracht_link_vali* link, IPCAddress_t*);

// OS API
struct sockaddr_storage;
GRACHTAPI int gracht_os_get_server_client_address(struct sockaddr_storage*, int*);
GRACHTAPI int gracht_os_get_server_packet_address(struct sockaddr_storage*, int*);

#ifdef __cplusplus
}
#endif
#endif // !__GRACHT_LINK_VALI_H__
