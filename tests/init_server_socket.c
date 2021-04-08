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
#include <gracht/server.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#if defined(__linux__)
#include <sys/un.h>

static const char* dgramPath = "/tmp/g_dgram";
static const char* clientsPath = "/tmp/g_clients";

static void init_socket_config(struct socket_server_configuration* socketConfig)
{
    struct sockaddr_un* dgramAddr = (struct sockaddr_un*)&socketConfig->dgram_address;
    struct sockaddr_un* serverAddr = (struct sockaddr_un*)&socketConfig->server_address;
    
    socketConfig->dgram_address_length = sizeof(struct sockaddr_un);
    socketConfig->server_address_length = sizeof(struct sockaddr_un);
    
    // Setup path for dgram
    unlink(dgramPath);
    dgramAddr->sun_family = AF_LOCAL;
    strncpy (dgramAddr->sun_path, dgramPath, sizeof(dgramAddr->sun_path));
    dgramAddr->sun_path[sizeof(dgramAddr->sun_path) - 1] = '\0';
    
    // Setup path for serverAddr
    unlink(clientsPath);
    serverAddr->sun_family = AF_LOCAL;
    strncpy (serverAddr->sun_path, clientsPath, sizeof(serverAddr->sun_path));
    serverAddr->sun_path[sizeof(serverAddr->sun_path) - 1] = '\0';
}

#elif defined(_WIN32)
#include <windows.h>

static void init_socket_config(struct socket_client_configuration* socketConfig)
{
    struct sockaddr_in* addrStream;
    struct sockaddr_in* addrPacket;
    
    // initialize the WSA library
    gracht_link_socket_initialize();

    addrStream = (struct sockaddr_in*)&socketConfig->server_address;
    addrPacket = (struct sockaddr_in*)&socketConfig->dgram_address;

    socketConfig->dgram_address_length = sizeof(struct sockaddr_un);
    socketConfig->server_address_length = sizeof(struct sockaddr_un);
    
    // AF_INET is the Internet address family.
    addrPacket->sin_family = AF_INET;
    addrPacket->sin_addr.s_addr = inet_addr("127.0.0.1");
    addrPacket->sin_port = htons(55554);

    // AF_INET is the Internet address family.
    addrStream->sin_family = AF_INET;
    addrStream->sin_addr.s_addr = inet_addr("127.0.0.1");
    addrStream->sin_port = htons(55555);
}
#endif

int init_server_with_socket_link(void)
{
    struct socket_server_configuration linkConfiguration = { 0 };
    struct gracht_server_configuration serverConfiguration;
    int                                code;
    
    gracht_server_configuration_init(&serverConfiguration);
    init_socket_config(&linkConfiguration);
    
    gracht_link_socket_server_create(&serverConfiguration.link, &linkConfiguration);
    code = gracht_server_initialize(&serverConfiguration);
    if (code) {
        printf("init_server_with_socket_link: error initializing server library %i\n", errno);
    }
    return code;
}
