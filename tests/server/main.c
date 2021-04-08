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

#include <test_utils_protocol_server.h>

extern int init_server_with_socket_link(void);

void test_utils_print_callback(struct gracht_recv_message* message, struct test_utils_print_args*);

static gracht_protocol_function_t test_utils_callbacks[1] = {
    { PROTOCOL_TEST_UTILS_PRINT_ID , test_utils_print_callback },
};
DEFINE_TEST_UTILS_SERVER_PROTOCOL(test_utils_callbacks, 1);

void test_utils_print_callback(struct gracht_recv_message* message, struct test_utils_print_args* args)
{
    printf("print: received message: %s\n", args->message);
    test_utils_print_response(message, strlen(args->message));
}

int main(void)
{
    int code;
    
    // initialize server
    code = init_server_with_socket_link();
    if (code) {
        return code;
    }
    
    // register protocols
    gracht_server_register_protocol(&test_utils_server_protocol);

    // run server
    return gracht_server_main_loop();
}
