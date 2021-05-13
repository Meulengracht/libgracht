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
 * Gracht OS Type Definitions & Structures
 * - This header describes the base os-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include "gracht/link/vali.h"
#include "private.h"
#include <inet/socket.h>
#include <inet/local.h>
#include <stdlib.h>
#include <string.h>

extern void gracht_link_client_vali_api(struct gracht_link_vali*);
extern void gracht_link_server_vali_api(struct gracht_link_vali*);

int gracht_os_get_server_client_address(struct sockaddr_storage* address, int* address_length_out)
{
    struct sockaddr_lc* local_address = sstolc(address);
    *address_length_out               = sizeof(struct sockaddr_lc);

    // Prepare the server address.
    memset(local_address, 0, sizeof(struct sockaddr_lc));
    memcpy(&local_address->slc_addr[0], LCADDR_WM0, strlen(LCADDR_WM0));
    local_address->slc_len    = sizeof(struct sockaddr_lc);
    local_address->slc_family = AF_LOCAL;
    return 0;
}

int gracht_os_get_server_packet_address(struct sockaddr_storage* address, int* address_length_out)
{
    struct sockaddr_lc* local_address = sstolc(address);
    *address_length_out               = sizeof(struct sockaddr_lc);

    // Prepare the server address. 
    memset(local_address, 0, sizeof(struct sockaddr_lc));
    memcpy(&local_address->slc_addr[0], LCADDR_WM1, strlen(LCADDR_WM1));
    local_address->slc_len    = sizeof(struct sockaddr_lc);
    local_address->slc_family = AF_LOCAL;
    return 0;
}

int gracht_link_vali_create(struct gracht_link_vali** linkOut)
{
    struct gracht_link_vali* link;

    link = (struct gracht_link_vali*)malloc(sizeof(struct gracht_link_vali));
    if (!link) {
        errno = ENOMEM;
        return -1;
    }

    memset(link, 0, sizeof(struct gracht_link_vali));
    link->iod = -1;
    link->base.type = gracht_link_packet_based;
    gracht_link_client_vali_api(link);

    *linkOut = link;
    return 0;
}

void gracht_link_vali_set_listen(struct gracht_link_vali* link, int listen)
{
    if (listen) {
        gracht_link_server_vali_api(link);
    }
    else {
        gracht_link_client_vali_api(link);
    }
}

void gracht_link_vali_set_address(struct gracht_link_vali* link, struct ipmsg_addr* address)
{
    memcpy(&link->address, address, sizeof(struct ipmsg_addr));
}

#ifdef GRACHT_SHARED_LIBRARY
void dllmain(int action)
{
    _CRT_UNUSED(action);
}
#endif
