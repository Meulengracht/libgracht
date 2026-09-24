/**
 * Copyright 2021, Philip Meulengracht
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

#ifndef __GRACHT_VALI_PRIVATE_H__
#define __GRACHT_VALI_PRIVATE_H__

#include <gracht/link/vali.h>
#include <utils.h>

struct gracht_link_vali {
    struct gracht_link base;
    IPCAddress_t       address;
    uint32_t           send_timeout_ms;
};

/**
 * @brief Shared deadline handling for requests, responses and directed events. 
 * @param iod The I/O descriptor for the IPC endpoint.
 * @param address The address of the IPC recipient.
 * @param data Pointer to the data to be sent.
 * @param length Length of the data to be sent.
 * @param timeoutMs Timeout in milliseconds for the send operation.
 * @return 0 on success, or -1 on failure with errno set appropriately.
 */
int gracht_vali_send(int iod, IPCAddress_t* address, const void* data, unsigned int length, uint32_t timeoutMs);

#endif // !__GRACHT_VALI_PRIVATE_H__
