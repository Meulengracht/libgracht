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

extern struct test_account g_testAccount_JohnDoe;
extern struct test_payment g_testPayment_19;
extern int test_verify_account(const struct test_account* obtained, const struct test_account* expected);

extern int init_client_with_socket_link(gracht_client_t** clientOut);

static int __test_transfer(gracht_client_t* client)
{
    struct test_transaction       transaction;
    struct gracht_message_context context;
    struct test_transfer_status   status;

    test_transaction_init(&transaction);
    transaction.test_id = 12;
    
    test_utils_transfer(client, &context, &transaction);
    gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    test_utils_transfer_result(client, &context, &status);
    return status.test_id != 12;
}

static int __test_account(gracht_client_t* client)
{
    struct gracht_message_context context;
    struct test_account           account;
    
    test_utils_get_account(client, &context, "John Doe");
    gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    test_utils_get_account_result(client, &context, &account);
    return test_verify_account(&account, &g_testAccount_JohnDoe);
}

static int __test_payment(gracht_client_t* client)
{
    struct gracht_message_context context;
    int                           result;
    
    test_utils_add_payment(client, &context, &g_testAccount_JohnDoe, &g_testPayment_19);
    gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    test_utils_add_payment_result(client, &context, &result);
    return result;
}

int main(void)
{
    gracht_client_t*              client;
    int                           code;
    struct gracht_message_context context;

    // create client
    code = init_client_with_socket_link(&client);
    if (code) {
        return code;
    }

    // register protocols
    gracht_client_register_protocol(client, &test_utils_client_protocol);

    code = __test_transfer(client);
    if (code) {
        printf("transfer: failed\n");
        return code;
    }

    code = __test_account(client);
    if (code) {
        printf("account: failed\n");
        return code;
    }

    code = __test_payment(client);
    if (code) {
        printf("payment: failed\n");
        return code;
    }

    gracht_client_shutdown(client);
    return 0;
}

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
