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

static void init_socket_config(struct socket_client_configuration* socketConfig)
{
    struct sockaddr_un* addr = (struct sockaddr_un*)&socketConfig->address;
    socketConfig->address_length = sizeof(struct sockaddr_un);

    //socketConfig->type = gracht_link_packet_based;
    socketConfig->type = gracht_link_stream_based;

    addr->sun_family = AF_LOCAL;
    //strncpy (addr->sun_path, dgramPath, sizeof(addr->sun_path));
    strncpy (addr->sun_path, clientsPath, sizeof(addr->sun_path));
    addr->sun_path[sizeof(addr->sun_path) - 1] = '\0';
}

#elif defined(_WIN32)
#include <windows.h>

static void init_socket_config(struct socket_client_configuration* socketConfig)
{
    struct sockaddr_in* addr;
    
    // initialize the WSA library
    gracht_link_socket_initialize();

    addr = (struct sockaddr_in*)&socketConfig->address;
    socketConfig->address_length = sizeof(struct sockaddr_in);
    
    //socketConfig->type = gracht_link_packet_based;
    socketConfig->type = gracht_link_stream_based;

    // AF_INET is the Internet address family.
    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = inet_addr("127.0.0.1");
    addr->sin_port = htons(55555);
}
#endif

int init_client_with_socket_link(gracht_client_t** clientOut)
{
    struct socket_client_configuration linkConfiguration = { 0 };
    struct gracht_client_configuration clientConfiguration = { 0 };
    gracht_client_t*                   client = NULL;
    int                                code;

    init_socket_config(&linkConfiguration);
    gracht_link_socket_client_create(&clientConfiguration.link, &linkConfiguration);
    code = gracht_client_create(&clientConfiguration, &client);
    if (code) {
        printf("init_client_with_socket_link: error initializing client library %i, %i\n", errno, code);
    }
    *clientOut = client;
    return code;
}
