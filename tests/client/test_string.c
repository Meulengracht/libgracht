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

static int __test_print(gracht_client_t* client, const char* string)
{
    struct gracht_message_context context;
    int code, status = -1337;

    code = test_utils_print(client, &context, string);
    if (code) {
        return code;
    }

    gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    test_utils_print_result(client, &context, &status);
    if (status != strlen(string)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int __test_receive_string(gracht_client_t* client)
{
    struct gracht_message_context context;
    int code, status = -1337;
    char buffer[128];

    code = test_utils_receive_string(client, &context);
    if (code) {
        return code;
    }

    gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    test_utils_receive_string_result(client, &context, &buffer[0], sizeof(buffer));
    if (strcmp(buffer, "hello from test server!")) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    gracht_client_t* client;
    int              status;
    char*            text = "hello from wm_client!";

    if (argc > 1) {
        text = argv[1];
    }

    // create client
    status = init_client_with_socket_link(&client);
    if (status) {
        fprintf(stderr, "failed to create client: %s\n", strerror(status));
        return status;
    }

    // register protocols
    gracht_client_register_protocol(client, &test_utils_client_protocol);

    status = __test_print(client, text);
    if (status) {
        fprintf(stderr, "__test_print: FAILED [%s]\n", strerror(status));
        return status;
    }

    status = __test_receive_string(client);
    if (status) {
        fprintf(stderr, "__test_receive_string: FAILED [%s]\n", strerror(status));
        return status;
    }

    gracht_client_shutdown(client);
    return status;
}
