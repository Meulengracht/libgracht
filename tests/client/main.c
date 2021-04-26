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

#include "test_utils_service_client.h"

extern int init_client_with_socket_link(gracht_client_t** clientOut);

int main(int argc, char **argv)
{
    gracht_client_t*              client;
    int                           code, status = -1337;
    struct gracht_message_context context;

    // create client
    code = init_client_with_socket_link(&client);
    if (code) {
        return code;
    }

    // register protocols

    // run test
    if (argc > 1) {
        code = test_utils_print(client, &context, argv[1]);
    }
    else {
        code = test_utils_print(client, &context, "hello from wm_client!");
    }

    gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    test_utils_print_result(client, &context, &status);
    
    printf("gracht_client: recieved status %i\n", status);
    gracht_client_shutdown(client);
    return 0;
}
