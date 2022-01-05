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
#include <gracht/server.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <test_utils_service_server.h>

extern int init_mt_server_with_socket_link(int workerCount, gracht_server_t** serverOut);

// reuse the private api
#include <thread_api.h>

static int wait_and_respond(void* context)
{
    struct gracht_message*      defer  = context;
    struct test_transfer_status status = {
        .test_id = 1000,
        .code = 13
    };

    printf("transfer: responding\n");
    test_utils_transfer_response(defer, &status);
    free(defer);
    return 0;
}

void test_utils_print_invocation(struct gracht_message* message, const char* text)
{
    printf("print: %s\n", text);
    test_utils_print_response(message, strlen(text));
}

void test_utils_transfer_invocation(struct gracht_message* message, const struct test_transaction* transaction)
{
    struct test_transfer_status status;
    thrd_t                      wait;
    struct gracht_message*      defer;

    printf("transfer: %u\n", transaction->test_id);

    if (transaction->test_id < 1000) {
        status.test_id = transaction->test_id;
        status.code = 13;
        test_utils_transfer_response(message, &status);
        return;
    }

    // handle deferring of messages
    defer = malloc(GRACHT_MESSAGE_DEFERRABLE_SIZE(message));
    if (!defer) {
        status.test_id = transaction->test_id;
        status.code = -(ENOMEM);
        test_utils_transfer_response(message, &status);
        return;
    }

    printf("transfer: deferring\n");
    gracht_server_defer_message(message, defer);
    thrd_create(&wait, wait_and_respond, defer);
}

void test_utils_transfer_many_invocation(struct gracht_message* message, const struct test_transaction* transactions, const uint32_t transactions_count)
{
    struct test_transfer_status* statuses;
    uint32_t                     i;

    statuses = (struct test_transfer_status*)malloc(sizeof(struct test_transfer_status) * transactions_count);
    for (i = 0; i < transactions_count; i++) {
        statuses[i].test_id = transactions[i].test_id;
        statuses[i].code = 13;
    }

    test_utils_transfer_many_response(message, &statuses[0], transactions_count);
    free(statuses);
}

void test_utils_transfer_data_invocation(struct gracht_message* message, const uint8_t* data, const uint32_t data_count)
{
    
}

void test_utils_receive_data_invocation(struct gracht_message* message)
{

}

void test_utils_get_event_invocation(struct gracht_message* message, const int count)
{
    printf("get_events: %i\n", count);
    for (int i = 0; i < count; i++) {
        test_utils_event_myevent_single(message->server, message->client, i);
    }
}

void test_utils_shutdown_invocation(struct gracht_message* message)
{
    printf("shutdown\n");
    gracht_server_request_shutdown(message->server);
}


int main(void)
{
    gracht_server_t* server;
    int              code;
    
    // initialize server
    code = init_mt_server_with_socket_link(4, &server);
    if (code) {
        return code;
    }
    
    // register protocols
    gracht_server_register_protocol(server, &test_utils_server_protocol);

    // run server
    return gracht_server_main_loop(server);
}
