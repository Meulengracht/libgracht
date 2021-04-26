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

#define NUM_PARALLEL_CALLS 10

extern int init_client_with_socket_link(gracht_client_t** clientOut);

static char* testMsg = "hello from wm_client!";

int main(int argc, char **argv)
{
    gracht_client_t*              client;
    char*                         msg = testMsg;
    int                           i, code, status = -1337;
    struct gracht_message_context  context[NUM_PARALLEL_CALLS];
    struct gracht_message_context* contexts[NUM_PARALLEL_CALLS];

    if (argc > 1) {
        msg = argv[1];
    }

    // create client
    code = init_client_with_socket_link(&client);
    if (code) {
        return code;
    }

    // register protocols

    // run test
    for (i = 0; i < NUM_PARALLEL_CALLS; i++) {
        contexts[i] = &context[i];
        code = test_utils_print(client, &context[i], msg);
        if (code) {
            printf("gracht_client: call %i failed with code %i\n", i, code);
        }
    }

    gracht_client_await_multiple(client, contexts, NUM_PARALLEL_CALLS, GRACHT_AWAIT_ALL);
    for (i = 0; i < NUM_PARALLEL_CALLS; i++) {
        test_utils_print_result(client, &context[i], &status);
        printf("gracht_client: call %i returned %i\n", i, status);
    }

    gracht_client_shutdown(client);
    return 0;
}
