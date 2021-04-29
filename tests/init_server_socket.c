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

static void init_packet_link_config(struct gracht_link_socket* link)
{
    struct sockaddr_un addr = { 0 };
    
    // Setup path for dgram
    unlink(dgramPath);
    addr.sun_family = AF_LOCAL;
    strncpy(addr.sun_path, dgramPath, sizeof(addr.sun_path));
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
    
    gracht_link_socket_set_type(link, gracht_link_packet_based);
    gracht_link_socket_set_address(link, (const struct sockaddr_storage*)&addr, sizeof(struct sockaddr_un));
    gracht_link_socket_set_listen(link, 1);
    gracht_link_socket_set_domain(link, AF_LOCAL);
}

static void init_client_link_config(struct gracht_link_socket* link)
{
    struct sockaddr_un addr = { 0 };
    
    // Setup path for serverAddr
    unlink(clientsPath);
    addr.sun_family = AF_LOCAL;
    strncpy(addr.sun_path, clientsPath, sizeof(addr.sun_path));
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
    
    gracht_link_socket_set_type(link, gracht_link_stream_based);
    gracht_link_socket_set_address(link, (const struct sockaddr_storage*)&addr, sizeof(struct sockaddr_un));
    gracht_link_socket_set_listen(link, 1);
    gracht_link_socket_set_domain(link, AF_LOCAL);
}

#elif defined(_WIN32)
#include <windows.h>

static void init_packet_link_config(struct gracht_link_socket* link)
{
    struct sockaddr_in addr = { 0 };
    
    // AF_INET is the Internet address family.
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(55554);

    gracht_link_socket_set_type(link, gracht_link_packet_based);
    gracht_link_socket_set_address(link, (const struct sockaddr_storage*)&addr, sizeof(struct sockaddr_in));
    gracht_link_socket_set_listen(link, 1);
}

static void init_client_link_config(struct gracht_link_socket* link)
{
    struct sockaddr_in addr = { 0 };
    
    // AF_INET is the Internet address family.
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(55555);

    gracht_link_socket_set_type(link, gracht_link_stream_based);
    gracht_link_socket_set_address(link, (const struct sockaddr_storage*)&addr, sizeof(struct sockaddr_in));
    gracht_link_socket_set_listen(link, 1);
}
#endif

void register_server_links(void)
{
    struct gracht_link_socket* clientLink;
    struct gracht_link_socket* packetLink;

    gracht_link_socket_create(&clientLink);
    gracht_link_socket_create(&packetLink);

    init_client_link_config(clientLink);
    init_packet_link_config(packetLink);

    gracht_server_add_link(&clientLink->base);
    gracht_server_add_link(&packetLink->base);
}

int init_server_with_socket_link(void)
{
    struct gracht_server_configuration serverConfiguration;
    int                                code;
    
#ifdef _WIN32
    // initialize the WSA library
    gracht_link_socket_setup();
#endif

    gracht_server_configuration_init(&serverConfiguration);
    
    code = gracht_server_initialize(&serverConfiguration);
    if (code) {
        printf("init_server_with_socket_link: error initializing server library %i\n", errno);
    }

    // register links
    register_server_links();
    return code;
}

int init_mt_server_with_socket_link(int workerCount)
{
    struct gracht_server_configuration serverConfiguration;
    int                                code;
    
#ifdef _WIN32
    // initialize the WSA library
    gracht_link_socket_setup();
#endif

    gracht_server_configuration_init(&serverConfiguration);

    // setup the number of workers
    gracht_server_configuration_set_num_workers(&serverConfiguration, workerCount);
    code = gracht_server_initialize(&serverConfiguration);
    if (code) {
        printf("init_server_with_socket_link: error initializing server library %i\n", errno);
    }

    // register links
    register_server_links();
    return code;
}
