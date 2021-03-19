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
 * Gracht Client Type Definitions & Structures
 * - This header describes the base client-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_CLIENT_H__
#define __GRACHT_CLIENT_H__

#include "types.h"
#include "link/link.h"

typedef struct gracht_client_configuration {
    // Link operations, which can be filled by any link-implementation under <link/*>
    // these provide the underlying link implementation like a socket interface or a serial interface.
    struct client_link_ops* link;
} gracht_client_configuration_t;

// Prototype declaration to hide implementation details.
typedef struct gracht_client gracht_client_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Creates a new instance of a gracht client based on the link configuration. An application
 * can utilize multiple clients if it wants, though not required. A client can initiate multiple
 * messages and supports fragmented responses.
 * 
 * @param config The client configuration which has been initialized prior to this call.
 * @param clientOut Storage for the client pointer.
 * @return int Returns 0 if the creation was successful.
 */
int gracht_client_create(gracht_client_configuration_t* config, gracht_client_t** clientOut);

/**
 * Registers a new protocol with the client. A max of 255 protocols can be registered, and if
 * the client receives an event with an unsupported protocol it ignores the event. Only events that
 * match the protocol/function id pair is actually invoked.
 * 
 * @param protocol The protocol instance to register.
 * @return int Returns 0 if the protocol was registered, or -1 if max count of protocols has been registered.
 */
int gracht_client_register_protocol(gracht_client_t* client, gracht_protocol_t* protocol);

/**
 * Unregisters a previously registered protocol. Any messages targetted for that protocol will be ignored after
 * unregistering.
 * 
 * @param protocol The protocol that should be unregistered. 
 */
void gracht_client_unregister_protocol(gracht_client_t* client, gracht_protocol_t* protocol);

/**
 * Destroys a previously created client. This frees up the resources associated, and disconnects any outstanding
 * connections. Any messages in-air will be terminated.
 * 
 * @param client A pointer to a previously created gracht client.
 */
void gracht_client_shutdown(gracht_client_t* client);

/**
 * Returns the associated connection handle/descriptor that the clients uses. This can be
 * usefull if an application wants to support async transfers with epoll/select/completion ports.
 * 
 * @param client A pointer to a previously created gracht client.
 * @return int The connection descriptor/handle.
 */
int gracht_client_iod(gracht_client_t* client);

/**
 * Wait for any incomming message and store the message recieved in the provided buffer. This function can
 * be used to block untill a message is recieved. This should not be invoked to wait for a specific message,
 * but rather be used to poll for new events. It is not mandatory to use this call if the client is not used
 * for events.
 * 
 * @param client A pointer to a previously created gracht client.
 * @param context The message context if required.
 * @param messageBuffer A pointer to buffer storage of size GRACHT_MAX_MESSAGE_SIZE. The buffer may not be smaller than this.
 * @param flags The flag GRACHT_WAIT_BLOCK can be specified to block untill a new message is received.
 * @return int Result of the wait. Returns 0 if a message was received and handled. Returns -1 if no messages are in queue.
 */
int gracht_client_wait_message(gracht_client_t *client, struct gracht_message_context *context, void *messageBuffer, unsigned int flags);


/**
 * Can be used to await a response for a specific function invoke. This only returns when the function response
 * was received.
 * 
 * @param client A pointer to a previously created gracht client.
 * @param context The message context that should be awaited.
 * @return int Status of the wait. Only returns -1 if there was any connection issues.
 */
int gracht_client_await(gracht_client_t* client, struct gracht_message_context* context);

/**
 * Can be used to await multiple response for function invokes. The client can initiate multiple function calls
 * and wait for one or all to complete. The wait operation is specified by the flags GRACHT_AWAIT_*.
 * 
 * @param client A pointer to a previously created gracht client.
 * @param context The message contexts that should be awaited.
 * @param count The number of message contexts that should be awaited. Must be >0.
 * @param flags The waiting mode that should be used.
 * @return int Status of the wait. Only returns -1 if there was any connection issues.
 */
int gracht_client_await_multiple(gracht_client_t* client, struct gracht_message_context** contexts, int count, unsigned int flags);

/**
 * Invoked by the generated protocol functions. This is the generic call interface that all functions are built on top of.
 * This should never be invoked manually, but require specific setup.
 * 
 * @param client A pointer to a previously created gracht client.
 * @param context The message context if any is required.
 * @param message The message format that is pre-built.
 * @return int Status of the invoke. Only returns -1 if any connection issues are detected.
 */
int gracht_client_invoke(gracht_client_t* client, struct gracht_message_context* context, struct gracht_message* message);

/**
 * Invoked by the generated protocol functions. This essentially unpacks any data recieved and stuffs
 * it correctly into the response structures.
 * 
 * @param client A pointer to a previously created gracht client.
 * @return int Result code of the status call.
 */
int gracht_client_status(gracht_client_t* client, struct gracht_message_context*, struct gracht_param*);

#ifdef __cplusplus
}
#endif
#endif // !__GRACHT_CLIENT_H__
