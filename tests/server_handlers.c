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
#include <test_small_upload_service_server.h>
#include <test_large_download_service_server.h>

// reuse the private api
#include <thread_api.h>

static char* g_message = "hello from test server!";

#define TEST_SMALL_UPLOAD_RESOURCE_ID "small-upload-resource"
#define TEST_SMALL_UPLOAD_SESSION_ID  "small-upload-session"
#define TEST_SMALL_UPLOAD_CHUNK_SIZE  64U
#define TEST_SMALL_UPLOAD_TOTAL_SIZE  176U

#define TEST_LARGE_DOWNLOAD_RESOURCE_ID "large-download-resource"
#define TEST_LARGE_DOWNLOAD_SESSION_ID  "large-download-session"
#define TEST_LARGE_DOWNLOAD_CHUNK_SIZE  4096U
#define TEST_LARGE_DOWNLOAD_TOTAL_SIZE  5632U

struct stream_upload_state {
    char*        data;
    uint8_t*     chunk_received;
    unsigned int expected_size;
    unsigned int received_size;
    unsigned int expected_chunks;
    unsigned int received_chunks;
    int          active;
    int          finished;
    int          closed;
    int          completed;
};

static struct stream_upload_state g_smallUpload = { 0 };
static uint8_t                    g_largeDownload[TEST_LARGE_DOWNLOAD_TOTAL_SIZE];
static int                        g_largeDownloadInitialized = 0;

static uint8_t stream_test_byte(unsigned int index)
{
    return (uint8_t)((index * 29U + 7U) & 0xFFU);
}

static void initialize_large_download_data(void)
{
    unsigned int i;

    if (g_largeDownloadInitialized) {
        return;
    }

    for (i = 0; i < TEST_LARGE_DOWNLOAD_TOTAL_SIZE; ++i) {
        g_largeDownload[i] = stream_test_byte(i);
    }
    g_largeDownloadInitialized = 1;
}

static void finalize_small_upload_if_ready(void)
{
    unsigned int i;

    if (!g_smallUpload.active || !g_smallUpload.finished || !g_smallUpload.closed) {
        return;
    }

    if (g_smallUpload.received_size != g_smallUpload.expected_size ||
        g_smallUpload.received_chunks != g_smallUpload.expected_chunks) {
        return;
    }

    for (i = 0; i < g_smallUpload.received_size; ++i) {
        assert((uint8_t)g_smallUpload.data[i] == stream_test_byte(i));
    }

    free(g_smallUpload.chunk_received);
    free(g_smallUpload.data);
    memset(&g_smallUpload, 0, sizeof(g_smallUpload));
    g_smallUpload.completed = 1;
}

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
    assert(g_smallUpload.completed);
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

void test_small_upload_open_invocation(struct gracht_message* message, const char* resource_id, const unsigned int size)
{
    (void)message;

    assert(strcmp(resource_id, TEST_SMALL_UPLOAD_RESOURCE_ID) == 0);
    assert(size == TEST_SMALL_UPLOAD_TOTAL_SIZE);

    free(g_smallUpload.data);
    free(g_smallUpload.chunk_received);
    g_smallUpload.data = malloc(size);
    assert(g_smallUpload.data != NULL);
    g_smallUpload.chunk_received = calloc((size + TEST_SMALL_UPLOAD_CHUNK_SIZE - 1) / TEST_SMALL_UPLOAD_CHUNK_SIZE, sizeof(uint8_t));
    assert(g_smallUpload.chunk_received != NULL);

    g_smallUpload.expected_size = size;
    g_smallUpload.received_size = 0;
    g_smallUpload.expected_chunks = (size + TEST_SMALL_UPLOAD_CHUNK_SIZE - 1) / TEST_SMALL_UPLOAD_CHUNK_SIZE;
    g_smallUpload.received_chunks = 0;
    g_smallUpload.active = 1;
    g_smallUpload.finished = 0;
    g_smallUpload.closed = 0;
    g_smallUpload.completed = 0;

    test_small_upload_open_response(message, TEST_SMALL_UPLOAD_SESSION_ID);
}

void test_small_upload_write_chunk_invocation(struct gracht_message* message, const char* session_id, const unsigned int index, const uint8_t* data, const uint32_t data_count)
{
    unsigned int offset;
    unsigned int expected_count;

    (void)message;

    assert(g_smallUpload.active);
    assert(strcmp(session_id, TEST_SMALL_UPLOAD_SESSION_ID) == 0);
    assert(index < g_smallUpload.expected_chunks);

    offset = index * TEST_SMALL_UPLOAD_CHUNK_SIZE;
    expected_count = g_smallUpload.expected_size - offset;
    if (expected_count > TEST_SMALL_UPLOAD_CHUNK_SIZE) {
        expected_count = TEST_SMALL_UPLOAD_CHUNK_SIZE;
    }

    assert(data_count == expected_count);
    assert(g_smallUpload.chunk_received[index] == 0);

    if (data_count != 0) {
        memcpy(&g_smallUpload.data[offset], data, data_count);
    }

    g_smallUpload.chunk_received[index] = 1;
    g_smallUpload.received_size += data_count;
    g_smallUpload.received_chunks++;
    finalize_small_upload_if_ready();
}

void test_small_upload_finish_invocation(struct gracht_message* message, const char* session_id)
{
    (void)message;

    assert(g_smallUpload.active);
    assert(strcmp(session_id, TEST_SMALL_UPLOAD_SESSION_ID) == 0);

    g_smallUpload.finished = 1;
    finalize_small_upload_if_ready();
}

void test_small_upload_close_invocation(struct gracht_message* message, const char* session_id)
{
    (void)message;

    assert(g_smallUpload.active);
    assert(strcmp(session_id, TEST_SMALL_UPLOAD_SESSION_ID) == 0);

    g_smallUpload.closed = 1;
    finalize_small_upload_if_ready();
}

void test_large_download_open_invocation(struct gracht_message* message, const char* resource_id)
{
    assert(strcmp(resource_id, TEST_LARGE_DOWNLOAD_RESOURCE_ID) == 0);

    initialize_large_download_data();
    test_large_download_open_response(message, TEST_LARGE_DOWNLOAD_SESSION_ID, TEST_LARGE_DOWNLOAD_TOTAL_SIZE);
}

void test_large_download_read_chunk_invocation(struct gracht_message* message, const char* session_id, const unsigned int index)
{
    unsigned int offset = index * TEST_LARGE_DOWNLOAD_CHUNK_SIZE;
    uint32_t     count;

    assert(strcmp(session_id, TEST_LARGE_DOWNLOAD_SESSION_ID) == 0);
    assert(offset < TEST_LARGE_DOWNLOAD_TOTAL_SIZE);

    count = (uint32_t)(TEST_LARGE_DOWNLOAD_TOTAL_SIZE - offset);
    if (count > TEST_LARGE_DOWNLOAD_CHUNK_SIZE) {
        count = TEST_LARGE_DOWNLOAD_CHUNK_SIZE;
    }

    test_large_download_read_chunk_response(message, &g_largeDownload[offset], count);
}

void test_large_download_close_invocation(struct gracht_message* message, const char* session_id)
{
    (void)message;

    assert(strcmp(session_id, TEST_LARGE_DOWNLOAD_SESSION_ID) == 0);
}
