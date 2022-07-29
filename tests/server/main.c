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
 * Gracht Testing Suite
 * - Implementation of various test programs that verify behaviour of libgracht
 */

#include <gracht/link/socket.h>
#include <gracht/server.h>

#include <test_utils_service_server.h>

extern int init_server_with_socket_link(gracht_server_t** serverOut);

int main(void)
{
    gracht_server_t* server;
    int              code;
    
    // initialize server
    code = init_server_with_socket_link(&server);
    if (code) {
        return code;
    }
    
    // register protocols
    gracht_server_register_protocol(server, &test_utils_server_protocol);

    // run server
    return gracht_server_main_loop(server);
}
