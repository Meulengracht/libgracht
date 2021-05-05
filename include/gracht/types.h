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

/**
 * Handle shared library exporting for non-linux platforms
 */
#if defined(GRACHT_SHARED_LIBRARY) && (defined(_WIN32) || defined(MOLLENOS))
#ifdef GRACHT_BUILD
#define GRACHTAPI __declspec(dllexport)
#else
#define GRACHTAPI __declspec(dllimport)
#endif
#else
#define GRACHTAPI extern
#endif

#if defined(_WIN32)
typedef void*      gracht_handle_t;
typedef uintptr_t  gracht_conn_t;
#define GRACHT_HANDLE_INVALID NULL
#define GRACHT_CONN_INVALID   (uintptr_t)(~0)
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

#define GRACHT_MESSAGE_HEADER_SIZE 11
#define GRACHT_MESSAGE_DEFERRABLE_SIZE(message) (sizeof(struct gracht_message) + message->size)

/**
 * Internally used in the message serializers to note which type of
 * message is being sent. Not all types cause any different behaviour.
 */
#define MESSAGE_FLAG_TYPE(flags) ((flags) & 0x3)
#define MESSAGE_FLAG_SYNC     0x00000000
#define MESSAGE_FLAG_ASYNC    0x00000001
#define MESSAGE_FLAG_EVENT    0x00000002
#define MESSAGE_FLAG_RESPONSE 0x00000003

/**
 * The message status, this is returned by any function that directly
 * refers to a specific message. Error indiciates a transmission error
 * or a protocol error.
 */
#define GRACHT_MESSAGE_ERROR      -1
#define GRACHT_MESSAGE_CREATED    0
#define GRACHT_MESSAGE_INPROGRESS 1
#define GRACHT_MESSAGE_COMPLETED  2

/**
 * Await flags used to configure the waiting mode when calling the clients
 * gracht_client_await(_multiple). Async mode should always be specificed
 * if the client has a dedicated thread running on the side waiting for messages.
 */
#define GRACHT_AWAIT_ANY   0x0
#define GRACHT_AWAIT_ALL   0x1
#define GRACHT_AWAIT_ASYNC 0x2

/**
 * Message flags used for waiting for incoming messages. The block flag can be
 * specified for gracht_client_wait_message to indicate the call should block
 * untill a message has been recieved.
 */
#define GRACHT_MESSAGE_BLOCK   0x1
#define GRACHT_MESSAGE_WAITALL 0x2

/**
 * The library is configured to use a default message of 2048 bytes. This
 * value is rather small to some, but it fits most needs.
 */
#define GRACHT_DEFAULT_MESSAGE_SIZE 2048

// Represents a received message on the server. What is relevant here and why
// the structure is exposed is when servers would like to respond to invocations
// in the form of events, they will access to the client member of this structure.
typedef struct gracht_server gracht_server_t;
struct gracht_message {
    gracht_server_t* server;  // server instance message is received on
    gracht_conn_t    link;    // link message is received on
    gracht_conn_t    client;  // client context on the link
    uint32_t         size;    // size of the payload
    uint32_t         index;   // used internally for payload storage
    uint8_t          payload[]; // payload follows this message header
};

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
