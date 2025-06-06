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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Gracht Socket Link Type Definitions & Structures
 * - This header describes the base link-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include "gracht/link/socket.h"
#include "logging.h"
#include "socket_os.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

#include <winsock2.h>

#define WS_VER 0x0202

static int g_wsaInititalized = 0;

int gracht_link_socket_setup(void)
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

// extern functions, this is the interfaces described in client.c/server.c
extern void gracht_link_client_socket_api(struct gracht_link_socket* link);
extern void gracht_link_server_socket_api(struct gracht_link_socket* link);

int gracht_link_socket_create(struct gracht_link_socket** linkOut)
{
    struct gracht_link_socket* link;
    
    link = (struct gracht_link_socket*)malloc(sizeof(struct gracht_link_socket));
    if (!link) {
        errno = ENOMEM;
        return -1;
    }

    memset(link, 0, sizeof(struct gracht_link_socket));
    gracht_link_client_socket_api(link);
    link->domain = AF_INET;
    link->base.connection = GRACHT_CONN_INVALID;

    *linkOut = link;
    return 0;
}

void gracht_link_socket_set_type(struct gracht_link_socket* link, enum gracht_link_type type)
{
    link->base.type = type;
}

void gracht_link_socket_set_listen(struct gracht_link_socket* link, int listen)
{
    link->listen = listen;
    if (listen) {
        gracht_link_server_socket_api(link);
    }
    else {
        gracht_link_client_socket_api(link);
    }
}

void gracht_link_socket_set_domain(struct gracht_link_socket* link, int socketDomain)
{
    link->domain = socketDomain;
}

void gracht_link_socket_set_bind_address(struct gracht_link_socket* link, const struct sockaddr_storage* address, socklen_t length)
{
    memcpy(&link->bind_address, address, length);
    link->bind_address_length = length;
}

void gracht_link_socket_set_connect_address(struct gracht_link_socket* link, const struct sockaddr_storage* address, socklen_t length)
{
    memcpy(&link->connect_address, address, sizeof(struct sockaddr_storage));
    link->connect_address_length = length;
}
