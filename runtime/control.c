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
 * Gracht Control Protocol Type Definitions & Structures
 * - This header describes the base control-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include "control.h"

#define SERIALIZE_VALUE(name, type) static inline void serialize_##name(gracht_buffer_t* buffer, type value) { \
                                  *((type*)&buffer->data[buffer->index]) = value; buffer->index += sizeof(type); \
                              }

#define DESERIALIZE_VALUE(name, type) static inline type deserialize_##name(gracht_buffer_t* buffer) { \
                                  type value = *((type*)&buffer->data[buffer->index]); \
                                  buffer->index += sizeof(type); \
                                  return value; \
                              }

SERIALIZE_VALUE(uint8_t, uint8_t)
DESERIALIZE_VALUE(uint8_t, uint8_t)
SERIALIZE_VALUE(int8_t, int8_t)
DESERIALIZE_VALUE(int8_t, int8_t)
SERIALIZE_VALUE(int32_t, int32_t)
DESERIALIZE_VALUE(int32_t, int32_t)
SERIALIZE_VALUE(uint32_t, uint32_t)
DESERIALIZE_VALUE(uint32_t, uint32_t)
SERIALIZE_VALUE(int, int)
DESERIALIZE_VALUE(int, int)

void __gracht_subscribe_internal(struct gracht_message* __message, gracht_buffer_t* __buffer);
void __gracht_unsubscribe_internal(struct gracht_message* __message, gracht_buffer_t* __buffer);
void __gracht_error_internal(gracht_client_t* __client, gracht_buffer_t* __buffer);

static gracht_protocol_function_t client_control_callbacks[1] = {
    { SERVICE_GRACHT_CONTROL_EVENT_ERROR_ID, __gracht_error_internal },
};

static gracht_protocol_function_t server_control_callbacks[2] = {
    { SERVICE_GRACHT_CONTROL_SUBSCRIBE_ID, __gracht_subscribe_internal },
    { SERVICE_GRACHT_CONTROL_UNSUBSCRIBE_ID, __gracht_unsubscribe_internal },
};

gracht_protocol_t gracht_control_client_protocol = GRACHT_PROTOCOL_INIT(0, "gracht_control", 1, client_control_callbacks);
gracht_protocol_t gracht_control_server_protocol = GRACHT_PROTOCOL_INIT(0, "gracht_control", 2, server_control_callbacks);

extern int gracht_server_get_buffer(gracht_server_t*, gracht_buffer_t*);
extern int gracht_server_send_event(gracht_server_t*, gracht_conn_t client, gracht_buffer_t*, unsigned int flags);
extern int gracht_server_broadcast_event(gracht_server_t*, gracht_buffer_t*, unsigned int flags);

void __gracht_error_internal(gracht_client_t* __client, gracht_buffer_t* __buffer)
{
    uint32_t __messageId;
    int __errorCode;
    __messageId = deserialize_uint32_t(__buffer);
    __errorCode = deserialize_int(__buffer);
    gracht_control_error_invocation(__client, __messageId, __errorCode);
}

void __gracht_subscribe_internal(struct gracht_message* __message, gracht_buffer_t* __buffer)
{
    uint8_t __protocol;
    __protocol = deserialize_uint8_t(__buffer);
    gracht_control_subscribe_invocation(__message, __protocol);
}

void __gracht_unsubscribe_internal(struct gracht_message* __message, gracht_buffer_t* __buffer)
{
    uint8_t __protocol;
    __protocol = deserialize_uint8_t(__buffer);
    gracht_control_unsubscribe_invocation(__message, __protocol);
}

int gracht_control_event_error_single(gracht_server_t* server, const gracht_conn_t client, const uint32_t messageId, const int errorCode)
{
    gracht_buffer_t __buffer;
    int             __status;

    __status = gracht_server_get_buffer(server, &__buffer);
    if (__status) {
        return __status;
    }

    serialize_uint32_t(&__buffer, 0);
    serialize_uint32_t(&__buffer, 0);
    serialize_uint8_t(&__buffer, 0);
    serialize_uint8_t(&__buffer, 2);
    serialize_uint8_t(&__buffer, MESSAGE_FLAG_EVENT);
    serialize_uint32_t(&__buffer, messageId);
    serialize_int(&__buffer, errorCode);
    __status = gracht_server_send_event(server, client, &__buffer, 0);
    return __status;
}

int gracht_control_event_error_all(gracht_server_t* server, const uint32_t messageId, const int errorCode)
{
    gracht_buffer_t __buffer;
    int             __status;

    __status = gracht_server_get_buffer(server, &__buffer);
    if (__status) {
        return __status;
    }

    serialize_uint32_t(&__buffer, 0);
    serialize_uint32_t(&__buffer, 0);
    serialize_uint8_t(&__buffer, 0);
    serialize_uint8_t(&__buffer, 2);
    serialize_uint8_t(&__buffer, MESSAGE_FLAG_EVENT);
    serialize_uint32_t(&__buffer, messageId);
    serialize_int(&__buffer, errorCode);
    __status = gracht_server_broadcast_event(server, &__buffer, 0);
    return __status;
}
