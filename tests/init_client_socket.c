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
 * LibGracht Client Test Code
 *  - Spawns a minimal implementation of a wm server to test libwm and the
 *    communication protocols in the os
 */

#include <gracht/link/socket.h>
#include <gracht/client.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#if defined(__linux__)
#include <sys/un.h>

//static const char* dgramPath = "/tmp/g_dgram";
static const char* clientsPath = "/tmp/g_clients";

static void init_socket_config(struct gracht_link_socket* link)
{
    struct sockaddr_un addr = { 0 };
    
    addr.sun_family = AF_LOCAL;
    //strncpy (addr->sun_path, dgramPath, sizeof(addr->sun_path));
    strncpy (addr.sun_path, clientsPath, sizeof(addr.sun_path));
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

    gracht_link_socket_set_type(link, gracht_link_stream_based);
    gracht_link_socket_set_connect_address(link, (const struct sockaddr_storage*)&addr, sizeof(struct sockaddr_un));
    gracht_link_socket_set_domain(link, AF_LOCAL);
}

#elif defined(_WIN32)
#include <windows.h>

static void init_socket_config(struct gracht_link_socket* link)
{
    struct sockaddr_in addr = { 0 };
    
    // initialize the WSA library
    gracht_link_socket_setup();

    // AF_INET is the Internet address family.
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(55555);

    gracht_link_socket_set_type(link, gracht_link_stream_based);
    gracht_link_socket_set_connect_address(link, (const struct sockaddr_storage*)&addr, sizeof(struct sockaddr_in));
}
#endif

int init_client_with_socket_link(gracht_client_t** clientOut)
{
    struct gracht_link_socket*         link;
    struct gracht_client_configuration clientConfiguration;
    gracht_client_t*                   client = NULL;
    int                                code;

    gracht_client_configuration_init(&clientConfiguration);
    
    gracht_link_socket_create(&link);
    init_socket_config(link);

    gracht_client_configuration_set_link(&clientConfiguration, (struct gracht_link*)link);

    code = gracht_client_create(&clientConfiguration, &client);
    if (code) {
        printf("init_client_with_socket_link: error initializing client library %i, %i\n", errno, code);
        return code;
    }

    code = gracht_client_connect(client);
    if (code) {
        printf("init_client_with_socket_link: failed to connect client %i, %i\n", errno, code);
    }

    *clientOut = client;
    return code;
}
