#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <gracht/client.h>

static int fail_after = -1;

static void* test_malloc(size_t size)
{
    if (fail_after == 0) return NULL;
    if (fail_after > 0) --fail_after;
    return malloc(size);
}

static void* test_calloc(size_t count, size_t size)
{
    if (fail_after == 0) return NULL;
    if (fail_after > 0) --fail_after;
    return calloc(count, size);
}

#define malloc test_malloc
#define calloc test_calloc
#include "test_utils_service.h"

static char wire[512];
static gracht_buffer_t response;
static unsigned int finalizations;
static unsigned int invocations;
static unsigned int event_invocations;
static int event_value;
static gracht_conn_t outer_sender_after_nested;

int gracht_client_get_status_buffer(gracht_client_t* client, struct gracht_message_context* context,
                                    gracht_buffer_t* buffer)
{
    (void)client;
    (void)context;
    *buffer = response;
    return GRACHT_MESSAGE_COMPLETED;
}

int gracht_client_status_finalize(gracht_client_t* client, gracht_buffer_t* buffer)
{
    (void)client;
    (void)buffer;
    ++finalizations;
    memset(wire, 0xDD, sizeof(wire));
    return 0;
}

#include "codec_helpers.h"

void test_utils_event_myevent_invocation(gracht_client_t* client, const int value)
{
    (void)client;
    ++event_invocations;
    event_value = value;
    if (value == -1) {
        char nested_wire[sizeof(int)];
        gracht_buffer_t nested = {nested_wire, 0, sizeof(nested_wire), 0, (gracht_conn_t)73};
        serialize_int(&nested, 123);
        nested.index = 0;
        __test_utils_myevent_internal(client, &nested);
        assert(!nested.error);
    }
}

void test_utils_transfer_many_invocation(struct gracht_message* message,
                                         const struct test_transaction* transactions,
                                         const uint32_t transactions_count)
{
    (void)message;
    (void)transactions;
    (void)transactions_count;
    ++invocations;
}

static gracht_buffer_t begin_message(void)
{
    memset(wire, 0, sizeof(wire));
    return (gracht_buffer_t){wire, GRACHT_MESSAGE_HEADER_SIZE, sizeof(wire), 0};
}

static void finish_message(gracht_buffer_t buffer)
{
    assert(!buffer.error);
    response = (gracht_buffer_t){wire, GRACHT_MESSAGE_HEADER_SIZE, buffer.index, 0};
}

static void transaction_response(uint32_t count)
{
    gracht_buffer_t buffer = begin_message();
    struct test_transaction transaction = {0};
    uint8_t bytes[] = {1, 2};
    transaction.serial = "owned";
    transaction.data = bytes;
    transaction.data_count = sizeof(bytes);
    serialize_uint32(&buffer, count);
    for (uint32_t index = 0; index < count; ++index) {
        transaction.test_id = index + 1;
        serialize_test_transaction(&buffer, &transaction);
    }
    serialize_uint32(&buffer, 99);
    finish_message(buffer);
}

static void test_scalars_and_strings(void)
{
    gracht_buffer_t boolean_buffer = begin_message();
    serialize_bool(&boolean_buffer, true);
    boolean_buffer.index = GRACHT_MESSAGE_HEADER_SIZE;
    assert(deserialize_bool(&boolean_buffer) == 1);
    for (uint32_t offset = 0; offset < 8; ++offset) {
        gracht_buffer_t buffer = {wire, offset, sizeof(wire), 0};
        serialize_uint64(&buffer, UINT64_MAX);
        serialize_double(&buffer, 1.25);
        buffer.index = offset;
        assert(deserialize_uint64(&buffer) == UINT64_MAX);
        assert(deserialize_double(&buffer) == 1.25 && !buffer.error);
    }
    for (uint32_t length = 0; length < sizeof(uint64_t); ++length) {
        gracht_buffer_t buffer = {wire, 0, length, 0};
        assert(deserialize_uint64(&buffer) == 0 && buffer.error == EMSGSIZE);
        assert(buffer.index == 0);
    }
    gracht_buffer_t buffer = {wire, 0, 3, 0};
    serialize_uint32(&buffer, 123);
    assert(buffer.error == EMSGSIZE && buffer.index == 0);
    buffer = begin_message();
    serialize_string(&buffer, "abc");
    finish_message(buffer);
    char output[] = "KEEP";
    deserialize_string_copy(&response, output, 0);
    assert(response.error == ENOBUFS && strcmp(output, "KEEP") == 0);
    response.index = GRACHT_MESSAGE_HEADER_SIZE;
    response.error = 0;
    wire[response.limit - 1] = 'x';
    assert(deserialize_string_nocopy(&response) == NULL && response.error == EPROTO);
    buffer = begin_message();
    serialize_uint32(&buffer, UINT32_MAX);
    finish_message(buffer);
    assert(deserialize_string_nocopy(&response) == NULL && response.error);
    assert(gracht_codec_size_array(8, 0x20000000u) == UINT32_MAX);
    assert(gracht_codec_size_add(UINT32_MAX - 1, 2) == UINT32_MAX);
    buffer = begin_message();
    assert(gracht_codec_array_bytes(&buffer, SIZE_MAX, 2) == 0 && buffer.error == EOVERFLOW);
}

static void test_results(void)
{
    for (uint32_t capacity = 0; capacity <= 3; ++capacity) {
        struct test_transaction items[3] = {0};
        uint32_t count = capacity;
        uint32_t trailer = 0;
        unsigned int previous = finalizations;
        transaction_response(2);
        int status = test_utils_probe_result(NULL, NULL, capacity ? items : NULL, &count, &trailer);
        assert(status == (capacity < 2 ? -1 : 0));
        if (capacity < 2) assert(errno == ENOBUFS);
        assert(count == (capacity < 2 ? capacity : 2));
        assert(trailer == 99 && finalizations == previous + 1);
        for (uint32_t index = 0; index < count; ++index) {
            assert(items[index].test_id == index + 1 && strcmp(items[index].serial, "owned") == 0);
            assert(items[index].data_count == 2 && items[index].data[1] == 2);
            test_transaction_destroy(&items[index]);
        }
    }
    struct test_transaction items[2] = {0};
    uint32_t count = 2;
    uint32_t trailer = 0;
    transaction_response(0);
    assert(test_utils_probe_result(NULL, NULL, items, &count, &trailer) == 0 && count == 0);
    transaction_response(2);
    --response.limit;
    count = 2;
    assert(test_utils_probe_result(NULL, NULL, items, &count, &trailer) == -1);
    assert(count == 0 && items[0].serial == NULL && items[1].data == NULL);
    transaction_response(2);
    count = 2;
    fail_after = 2;
    assert(test_utils_probe_result(NULL, NULL, items, &count, &trailer) == -1 && errno == ENOMEM);
    assert(count == 0 && items[0].serial == NULL);
    fail_after = -1;

    gracht_buffer_t buffer = begin_message();
    serialize_uint32(&buffer, 2);
    serialize_uint64(&buffer, 10);
    serialize_uint64(&buffer, 20);
    serialize_uint32(&buffer, 99);
    finish_message(buffer);
    uint64_t value = 0;
    count = 1;
    assert(test_utils_bytes_result(NULL, NULL, &value, &count, &trailer) == -1 && errno == ENOBUFS);
    assert(value == 10 && count == 1 && trailer == 99);

    buffer = begin_message();
    serialize_uint32(&buffer, UINT32_MAX);
    finish_message(buffer);
    count = 1;
    assert(test_utils_bytes_result(NULL, NULL, &value, &count, &trailer) == -1 && count == 0);

    buffer = begin_message();
    serialize_uint32(&buffer, 2);
    serialize_string(&buffer, "first");
    serialize_string(&buffer, "second");
    serialize_uint32(&buffer, 99);
    finish_message(buffer);
    char* strings[1] = {0};
    count = 1;
    assert(test_utils_strings_result(NULL, NULL, strings, &count, &trailer) == -1 && errno == ENOBUFS);
    assert(count == 1 && trailer == 99 && strcmp(strings[0], "first") == 0);
    free(strings[0]);
}

static void test_callbacks(void)
{
    transaction_response(2);
    __test_utils_transfer_many_internal(NULL, &response);
    assert(invocations == 1 && !response.error);
    transaction_response(2);
    response.limit -= 5;
    __test_utils_transfer_many_internal(NULL, &response);
    assert(invocations == 1 && response.error);
    transaction_response(2);
    fail_after = 2;
    __test_utils_transfer_many_internal(NULL, &response);
    assert(invocations == 1 && response.error == ENOMEM);
    fail_after = -1;
    gracht_buffer_t buffer = begin_message();
    serialize_uint32(&buffer, UINT32_MAX);
    finish_message(buffer);
    __test_utils_transfer_many_internal(NULL, &response);
    assert(invocations == 1 && response.error);
    struct test_account_owner owner = {0};
    buffer = begin_message();
    serialize_uint8(&buffer, 255);
    finish_message(buffer);
    deserialize_test_account_owner(&response, &owner);
    assert(response.error == EPROTO);
    test_account_owner_destroy(&owner);
}

int main(void)
{
    test_scalars_and_strings();
    test_results();
    test_callbacks();
    for (int endpoint = 41; endpoint <= 42; ++endpoint) {
        gracht_buffer_t buffer = begin_message();
        serialize_int(&buffer, endpoint + 100);
        finish_message(buffer);
        response.sender = (gracht_conn_t)endpoint;
        __test_utils_myevent_internal(NULL, &response);
        assert(event_value == endpoint + 100);
    }
    assert(event_invocations == 2);
    response.index = response.limit;
    response.sender = (gracht_conn_t)99;
    __test_utils_myevent_internal(NULL, &response);
    assert(response.error && event_invocations == 2);
    gracht_buffer_t buffer = begin_message();
    serialize_int(&buffer, -1);
    finish_message(buffer);
    response.sender = (gracht_conn_t)41;
    __test_utils_myevent_internal(NULL, &response);
    assert(!response.error && event_invocations == 4);
    return 0;
}