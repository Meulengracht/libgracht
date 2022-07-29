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

#include <assert.h>
#include <errno.h>
#include <gracht/server.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <test_utils_service_server.h>

// reuse the private api
#include <thread_api.h>

static char* g_message = "hello from test server!";

static int wait_and_respond(void* context)
{
    struct gracht_message*      defer  = context;
    struct test_transfer_status status = {
        .test_id = 1000,
        .code = 13
    };
    
    test_utils_transfer_response(defer, &status);
    free(defer);
    return 0;
}

void test_utils_print_invocation(struct gracht_message* message, const char* text)
{
    g_message = strdup(text);
    test_utils_print_response(message, strlen(text));
}

void test_utils_transfer_invocation(struct gracht_message* message, const struct test_transaction* transaction)
{
    struct test_transfer_status status;
    thrd_t                      wait;
    struct gracht_message*      defer;

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

    gracht_server_defer_message(message, defer);
    thrd_create(&wait, wait_and_respond, defer);
}

void test_utils_transfer_many_invocation(struct gracht_message* message, const struct test_transaction* transactions, const uint32_t transactions_count)
{
    struct test_transfer_status* statuses;
    uint32_t                     i;

    statuses = (struct test_transfer_status*)malloc(sizeof(struct test_transfer_status) * transactions_count);
    assert(statuses != NULL);

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
    char tmp[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    test_utils_receive_data_response(message, &tmp[0], sizeof(tmp));
}

void test_utils_receive_string_invocation(struct gracht_message* message)
{
    test_utils_receive_string_response(message, g_message);
}

void test_utils_get_event_invocation(struct gracht_message* message, const int count)
{
    for (int i = 0; i < count; i++) {
        test_utils_event_myevent_single(message->server, message->client, i);
    }
}

void test_utils_shutdown_invocation(struct gracht_message* message)
{
    printf("shutdown requested\n");
    gracht_server_request_shutdown(message->server);
}

extern struct test_account g_testAccount_JohnDoe;
extern struct test_payment g_testPayment_19;

extern int test_verify_payment(const struct test_payment* obtained, const struct test_payment* expected);
extern int test_verify_account(const struct test_account* obtained, const struct test_account* expected);

void test_utils_get_account_invocation(struct gracht_message* message, const char* name)
{
    if (strcmp(name, "John Doe") == 0) {
        test_utils_get_account_response(message, &g_testAccount_JohnDoe);
    } else {
        printf("get_account failed for unknown account: %s\n", name);
        assert(0);
    }
}

void test_utils_add_payment_invocation(struct gracht_message* message, const struct test_account* account, const struct test_payment* payment)
{
    if (strcmp(account->name, "primary account") == 0) {
        assert(test_verify_payment(payment, &g_testPayment_19) == 0);
        assert(test_verify_account(account, &g_testAccount_JohnDoe) == 0);
        test_utils_add_payment_response(message, 0);
    } else {
        printf("add_payment failed for unknown account: %s\n", account->name);
        test_utils_add_payment_response(message, -1);
    }
}
