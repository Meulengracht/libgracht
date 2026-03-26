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
#include <gracht/client.h>
#include <gracht/link/socket.h>
#include <stdio.h>
#include <string.h>

#include "test_utils_service_client.h"
#include "test_large_download_service_client.h"
#include "test_small_upload_service_client.h"

#define TEST_SMALL_UPLOAD_RESOURCE_ID "small-upload-resource"
#define TEST_SMALL_UPLOAD_SESSION_ID  "small-upload-session"
#define TEST_SMALL_UPLOAD_CHUNK_SIZE  64U
#define TEST_SMALL_UPLOAD_TOTAL_SIZE  176U

#define TEST_LARGE_DOWNLOAD_RESOURCE_ID "large-download-resource"
#define TEST_LARGE_DOWNLOAD_SESSION_ID  "large-download-session"
#define TEST_LARGE_DOWNLOAD_CHUNK_SIZE  4096U
#define TEST_LARGE_DOWNLOAD_TOTAL_SIZE  5632U

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

static uint8_t stream_test_byte(unsigned int index)
{
    return (uint8_t)((index * 29U + 7U) & 0xFFU);
}

static int test_small_upload_flow(gracht_client_t* client)
{
    struct gracht_message_context context;
    char                          session_id[64];
    uint8_t                       chunk[TEST_SMALL_UPLOAD_CHUNK_SIZE];
    unsigned int                  offset;
    unsigned int                  index;
    int                           status;

    status = test_small_upload_open(client, &context, TEST_SMALL_UPLOAD_RESOURCE_ID, TEST_SMALL_UPLOAD_TOTAL_SIZE);
    if (status) {
        return status;
    }

    status = gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    if (status) {
        return status;
    }

    status = test_small_upload_open_result(client, &context, &session_id[0], sizeof(session_id));
    if (status) {
        return status;
    }

    if (strcmp(session_id, TEST_SMALL_UPLOAD_SESSION_ID) != 0) {
        return EINVAL;
    }

    offset = 0;
    index = 0;
    while (offset < TEST_SMALL_UPLOAD_TOTAL_SIZE) {
        unsigned int count = TEST_SMALL_UPLOAD_TOTAL_SIZE - offset;
        unsigned int i;

        if (count > TEST_SMALL_UPLOAD_CHUNK_SIZE) {
            count = TEST_SMALL_UPLOAD_CHUNK_SIZE;
        }

        for (i = 0; i < count; ++i) {
            chunk[i] = stream_test_byte(offset + i);
        }

        status = test_small_upload_write_chunk(client, NULL, session_id, index, &chunk[0], count);
        if (status) {
            return status;
        }

        offset += count;
        index++;
    }

    status = test_small_upload_finish(client, NULL, session_id);
    if (status) {
        return status;
    }

    return test_small_upload_close(client, NULL, session_id);
}

static int test_large_download_flow(gracht_client_t* client)
{
    struct gracht_message_context context;
    char                          session_id[64];
    uint8_t                       chunk[TEST_LARGE_DOWNLOAD_CHUNK_SIZE];
    unsigned int                  size;
    unsigned int                  offset;
    unsigned int                  index;
    int                           status;

    status = test_large_download_open(client, &context, TEST_LARGE_DOWNLOAD_RESOURCE_ID);
    if (status) {
        return status;
    }

    status = gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    if (status) {
        return status;
    }

    status = test_large_download_open_result(client, &context, &session_id[0], sizeof(session_id), &size);
    if (status) {
        return status;
    }

    if (strcmp(session_id, TEST_LARGE_DOWNLOAD_SESSION_ID) != 0 || size != TEST_LARGE_DOWNLOAD_TOTAL_SIZE) {
        return EINVAL;
    }

    offset = 0;
    index = 0;
    while (offset < size) {
        unsigned int expected_count = size - offset;
        unsigned int i;

        if (expected_count > TEST_LARGE_DOWNLOAD_CHUNK_SIZE) {
            expected_count = TEST_LARGE_DOWNLOAD_CHUNK_SIZE;
        }

        memset(&chunk[0], 0xCD, sizeof(chunk));

        status = test_large_download_read_chunk(client, &context, session_id, index);
        if (status) {
            return status;
        }

        status = gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
        if (status) {
            return status;
        }

        status = test_large_download_read_chunk_result(client, &context, &chunk[0], sizeof(chunk));
        if (status) {
            return status;
        }

        for (i = 0; i < expected_count; ++i) {
            if (chunk[i] != stream_test_byte(offset + i)) {
                return EINVAL;
            }
        }

        for (i = expected_count; i < TEST_LARGE_DOWNLOAD_CHUNK_SIZE; ++i) {
            if (chunk[i] != 0xCD) {
                return EINVAL;
            }
        }

        offset += expected_count;
        index++;
    }

    return test_large_download_close(client, NULL, session_id);
}

int main(void)
{
    gracht_client_t* client;
    int              status;

    status = init_client_with_socket_link(&client);
    if (status) {
        fprintf(stderr, "failed to create client: %s\n", strerror(status));
        return status;
    }

    status = test_small_upload_flow(client);
    if (status) {
        fprintf(stderr, "test_small_upload_flow: FAILED [%s]\n", strerror(status));
        gracht_client_shutdown(client);
        return status;
    }
    status = test_large_download_flow(client);
    if (status) {
        fprintf(stderr, "test_large_download_flow: FAILED [%s]\n", strerror(status));
        gracht_client_shutdown(client);
        return status;
    }
    gracht_client_shutdown(client);
    return 0;
}