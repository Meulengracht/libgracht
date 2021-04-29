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

#ifndef __GRACHT_LINK_SOCKET_CLIENT_H__
#define __GRACHT_LINK_SOCKET_CLIENT_H__

#if defined(MOLLENOS)
#include <inet/socket.h>
#include <io.h>
#elif defined(__linux__)
#include <unistd.h>
#include <sys/socket.h>
#elif defined(_WIN32)
#include <winsock2.h>         
typedef int socklen_t;
#else
#error "Undefined platform for socket"
#endif

#include "link.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
/**
 * Only used on windows so far, and is used to initialize the WSA socket library.
 * 
 * @return int 
 */
int gracht_link_socket_setup(void);

/**
 * Only used on windows so far, and is used to cleanup the WSA socket library.
 * 
 * @return int 
 */
int gracht_link_socket_cleanup(void);
#endif

/**
 * Represents the socket link datastructure, and can be configured to work
 * however wanted. The default configuration is non-listen, connection-less mode
 * and the socket domain is AF_INET.
 */
struct gracht_link_socket {
    struct gracht_link      base;
    int                     listen;
    int                     domain;
    struct sockaddr_storage address;
    socklen_t               address_length;
};

int  gracht_link_socket_create(struct gracht_link_socket** linkOut);
void gracht_link_socket_set_type(struct gracht_link_socket* link, enum gracht_link_type type);
void gracht_link_socket_set_listen(struct gracht_link_socket* link, int listen);
void gracht_link_socket_set_domain(struct gracht_link_socket* link, int socketDomain);
void gracht_link_socket_set_address(struct gracht_link_socket* link, const struct sockaddr_storage* address, socklen_t length);

#ifdef __cplusplus
}
#endif
#endif // !__GRACHT_LINK_SOCKET_CLIENT_H__
