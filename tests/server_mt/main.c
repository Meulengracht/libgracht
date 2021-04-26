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
 * WM Server test
 *  - Spawns a minimal implementation of a wm server to test libwm and the
 *    communication protocols in the os
 */

#include <errno.h>
#include <gracht/link/socket.h>
#include <gracht/server.h>
#include <stdio.h>
#include <string.h>

#include <test_utils_service_server.h>

extern int init_mt_server_with_socket_link(int workerCount);

void test_utils_print_invocation(struct gracht_recv_message* message, const char* text)
{
    printf("print: received message: %s\n", text);
    test_utils_print_response(message, strlen(text));
}

int main(void)
{
    int code;
    
    // initialize server
    code = init_mt_server_with_socket_link(4);
    if (code) {
        return code;
    }
    
    // register protocols
    gracht_server_register_protocol(&test_utils_server_protocol);

    // run server
    return gracht_server_main_loop();
}
