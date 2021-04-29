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
    struct gracht_link* link;

    // <send_buffer>      if set, provides a buffer that the client should use for sending messages. The size of this
    //                    buffer must be provided in max_message_size. This buffer is not freed upon calling gracht_client_shutdown
    // <recv_buffer>      if set, provides a buffer that the client should use for receiving messages. The size of this
    //                    buffer must be atleast twice of max_message_size. 
    // <max_message_size> specifies the maximum message size that can be handled at once. If not set it defaults
    //                    to GRACHT_DEFAULT_MESSAGE_SIZE as the default value.
    void*               send_buffer;
    void*               recv_buffer;
    int                 recv_buffer_size;
    int                 max_message_size;
} gracht_client_configuration_t;

// Prototype declaration to hide implementation details.
typedef struct gracht_client gracht_client_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Configuration interface, use these as helpers instead of accessing the raw structure.
 */
void gracht_client_configuration_init(gracht_client_configuration_t* config);
void gracht_client_configuration_set_link(gracht_client_configuration_t* config, struct gracht_link* link);
void gracht_client_configuration_set_send_buffer(gracht_client_configuration_t* config, void* buffer);
void gracht_client_configuration_set_recv_buffer(gracht_client_configuration_t* config, void* buffer, int size);
void gracht_client_configuration_set_max_msg_size(gracht_client_configuration_t* config, int maxMessageSize);

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
 * Connects the client to the configured server.
 * 
 * @param client A client previously created by using gracht_client_create.
 * @return int Returns 0 if the connection was successful
 */
int gracht_client_connect(gracht_client_t* client);

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
 * @return gracht_conn_t The connection descriptor/handle.
 */
gracht_conn_t gracht_client_iod(gracht_client_t* client);

/**
 * Wait for any incomming message. This function can be used to block untill a message is recieved. 
 * This should not be invoked to wait for a specific message, but rather be used to poll for new events. 
 * It is not mandatory to use this call if the client is not used for events.
 * 
 * @param client A pointer to a previously created gracht client.
 * @param context The message context if required.
 * @param flags The flag GRACHT_MESSAGE_BLOCK can be specified to block untill a new message is received.
 * @return int Result of the wait. Returns 0 if a message was received and handled. Returns -1 if no messages are in queue.
 */
int gracht_client_wait_message(gracht_client_t *client, struct gracht_message_context *context, unsigned int flags);

/**
 * Can be used to await a response for a specific function invoke. This only returns when the function response
 * was received. The default waiting mode is synchronous, and that means that this function internally calls 
 * wait_message unless an asynchronous mode was specified.
 * 
 * @param client A pointer to a previously created gracht client.
 * @param context The message context that should be awaited.
 * @param flags The waiting mode that should be used.
 * @return int Status of the wait. Only returns -1 if there was any connection issues.
 */
int gracht_client_await(gracht_client_t* client, struct gracht_message_context* context, unsigned int flags);

/**
 * Can be used to await multiple response for function invokes. The client can initiate multiple function calls
 * and wait for one or all to complete. The wait operation is specified by the flags GRACHT_AWAIT_*. The default
 * waiting mode is synchronous, and that means that this function internally calls wait_message unless an asynchronous
 * mode was specified.
 * 
 * @param client A pointer to a previously created gracht client.
 * @param context The message contexts that should be awaited.
 * @param count The number of message contexts that should be awaited. Must be >0.
 * @param flags The waiting mode that should be used.
 * @return int Status of the wait. Only returns -1 if there was any connection issues.
 */
int gracht_client_await_multiple(gracht_client_t* client, struct gracht_message_context** contexts, int count, unsigned int flags);

#ifdef __cplusplus
}
#endif
#endif // !__GRACHT_CLIENT_H__
