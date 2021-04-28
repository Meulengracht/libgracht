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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Gracht Testing Suite
 * - Implementation of various test programs that verify behaviour of libgracht
 */

#include <errno.h>
#include <gracht/link/socket.h>
#include <gracht/client.h>
#include <stdio.h>
#include <string.h>

#include "test_utils_service_client.h"

extern int init_client_with_socket_link(gracht_client_t** clientOut);

void test_utils_event_myevent_invocation(gracht_client_t* client, const int n)
{
    (void)client;
    (void)n;
}

void test_utils_event_transfer_status_invocation(gracht_client_t* client, const struct test_transfer_status* transfer_status)
{
    (void)client;
    (void)transfer_status;
}

int main(void)
{
    gracht_client_t*              client;
    int                           code;
    struct gracht_message_context context;
    struct test_transaction       transactions[12];
    struct test_transfer_status   status[12];

    // create client
    code = init_client_with_socket_link(&client);
    if (code) {
        return code;
    }

    // register protocols
    gracht_client_register_protocol(client, &test_utils_client_protocol);

    // run test
    for (code = 0; code < 12; code++) {
        transactions[code].test_id = code + 1;
    }
    
    test_utils_transfer_many(client, &context, &transactions[0], 12);
    gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    test_utils_transfer_many_result(client, &context, &status[0], 12);

    for (code = 0; code < 12; code++) {
        printf("gracht_client: status[%i]=%i\n", code, status[code].code);
    }
    
    gracht_client_shutdown(client);
    return 0;
}
