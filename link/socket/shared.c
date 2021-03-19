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

#include "../include/gracht/link/socket.h"
#include "../include/gracht/debug.h"

#ifdef _WIN32

#include <winsock2.h>

#define WS_VER 0x0202

static int g_wsaInititalized = 0;

int gracht_link_socket_initialize(void)
{
    WSADATA wsd = {0};
    int status = WSAStartup(WS_VER, &wsd);
    if (status) {
        WSASetLastError(status);
        return -1;
    }
    g_wsaInititalized = 1;
    return 0;
}

int gracht_link_socket_cleanup(void)
{
    if (!g_wsaInititalized) {
        return -1;
    }

    WSACleanup();
    return 0;
}

#endif // _WIN32
