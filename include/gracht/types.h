/**
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
 * Gracht Type Definitions & Structures
 * - This header describes the base wm-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_TYPES_H__
#define __GRACHT_TYPES_H__

#include <stdint.h>
#include <stddef.h>

#if (defined (__clang__))
#define GRACHT_STRUCT(name, body) struct __attribute__((packed)) name body 
#elif (defined (__GNUC__))
#define GRACHT_STRUCT(name, body) struct name body __attribute__((packed))
#elif (defined (__arm__))
#define GRACHT_STRUCT(name, body) __packed struct name body
#elif (defined (_MSC_VER))
#define GRACHT_STRUCT(name, body) __pragma(pack(push, 1)) struct name body __pragma(pack(pop))
#else
#error "Please define packed struct for the used compiler"
#endif

#if defined(_WIN32)
typedef void*        gracht_handle_t;
typedef unsigned int gracht_conn_t;
#define GRACHT_HANDLE_INVALID NULL
#define GRACHT_CONN_INVALID   (unsigned int)0
#elif defined(MOLLENOS)
typedef int gracht_handle_t;
typedef int gracht_conn_t;
#define GRACHT_HANDLE_INVALID (int)-1
#define GRACHT_CONN_INVALID   (int)-1
#else
typedef int gracht_handle_t;
typedef int gracht_conn_t;
#define GRACHT_HANDLE_INVALID (int)-1
#define GRACHT_CONN_INVALID   (int)-1
#endif

#define MESSAGE_FLAG_TYPE(flags) (flags & 0x3)
#define MESSAGE_FLAG_SYNC     0x00000000
#define MESSAGE_FLAG_ASYNC    0x00000001
#define MESSAGE_FLAG_EVENT    0x00000002
#define MESSAGE_FLAG_RESPONSE 0x00000003

#define GRACHT_DEFAULT_MESSAGE_SIZE 512

#define GRACHT_AWAIT_ANY   0x0
#define GRACHT_AWAIT_ALL   0x1
#define GRACHT_AWAIT_ASYNC 0x2

#define GRACHT_MESSAGE_ERROR      -1
#define GRACHT_MESSAGE_CREATED    0
#define GRACHT_MESSAGE_INPROGRESS 1
#define GRACHT_MESSAGE_COMPLETED  2

#define GRACHT_MESSAGE_BLOCK   0x1
#define GRACHT_MESSAGE_WAITALL 0x2

#define GRACHT_MESSAGE_HEADER_SIZE 11

struct gracht_recv_message;

/**
 * The message buffer descriptor. Used internally by the generated system to perform
 * serialization and deserialization of messages
 */
typedef struct gracht_buffer {
    char*    data;
    uint32_t index;
} gracht_buffer_t;

/**
 * The context of a message. This is used as the message identifier when using
 * function calls that expect responses. The context that the message was invoked with
 * must be valid for the entire operation. Some links may have their own message contexts which
 * extend this one. Always use the one defined in the link header for the specific link used. 
 */
struct gracht_message_context {
    uint32_t message_id;
};

typedef struct gracht_protocol_function {
    uint8_t id;
    void*   address;
} gracht_protocol_function_t;

typedef struct gracht_protocol {
    uint8_t                     id;
    char*                       name;
    uint8_t                     num_functions;
    gracht_protocol_function_t* functions;
} gracht_protocol_t;

#define GRACHT_PROTOCOL_INIT(id, name, num_functions, functions) { id, name, num_functions, functions }

#endif // !__GRACHT_TYPES_H__
