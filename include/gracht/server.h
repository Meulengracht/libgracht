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
 * Gracht Server Type Definitions & Structures
 * - This header describes the base server-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_SERVER_H__
#define __GRACHT_SERVER_H__

#include "types.h"

struct gracht_server_callbacks {
    void (*clientConnected)(gracht_conn_t client);    // invoked only when a new stream-based client has connected
                                                      // or when a new connectionless-client has subscribed to the server
    void (*clientDisconnected)(gracht_conn_t client); // invoked only when a new stream-based client has disconnected
                                                      // or when a connectionless-client has unsubscribed from the server
};

typedef struct gracht_server_configuration {
    // Link operations, which can be filled by any link-implementation under <link/*>
    // these provide the underlying link implementation like a socket interface or a serial interface.
    struct server_link_ops*        link;

    // Callbacks are certain status updates the server can provide to the user of this library.
    // For instance when clients connect/disconnect. They are only invoked when set to non-null.
    struct gracht_server_callbacks callbacks;

    // Server configuration parameters, in this case the set descriptor (select/poll descriptor) to use
    // when the application wants control of the main loop and not use the gracht_server_main_loop function.
    // Then the application can manually call gracht_server_handle_event with the fd's that it does not handle.
    gracht_handle_t                set_descriptor;
    int                            set_descriptor_provided;

    // Server configuration parameters relating to the performance/capabilities of the server.
    // <server_workers>   specifies the number of worker-threads that will be used to handle requests. If 0 then
    //                    worker pool will not be created, and that means the server will handle incoming messages
    //                    on the current thread.
    // <max_message_size> specifies the maximum message size that can be handled at once. If not set it defaults
    //                    to GRACHT_MAX_MESSAGE_SIZE as the default value.
    int                            server_workers;
    int                            max_message_size;
} gracht_server_configuration_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Configuration interface, use these as helpers instead of accessing the raw structure.
 */
void gracht_server_configuration_init(gracht_server_configuration_t* config);
void gracht_server_configuration_set_link(gracht_server_configuration_t* config, struct server_link_ops* link);
void gracht_server_configuration_set_aio_descriptor(gracht_server_configuration_t* config, gracht_handle_t descriptor);
void gracht_server_configuration_set_num_workers(gracht_server_configuration_t* config, int workerCount);
void gracht_server_configuration_set_max_msg_size(gracht_server_configuration_t* config, int maxMessageSize);

/**
 * Initializes the global gracht server instance based on the config provided. The configuratipn
 * must provide the link operations (see link.h) and can optionally provide a poll/select handle (set_descriptor)
 * or server callbacks.
 * 
 * @param config The configuration structure that determines operation of the server.
 * @return int   Result of the initialize, returns 0 for success.
 */
int gracht_server_initialize(gracht_server_configuration_t* config);

/**
 * Registers a new protocol with the server. A max of 255 protocols can be registered, and if
 * the server is called with an unsupported protocol it ignores the message. Only messages that
 * match the protocol/function id pair is actually invoked.
 * 
 * @param protocol The protocol instance to register.
 * @return int Returns 0 if the protocol was registered, or -1 if max count of protocols has been registered.
 */
int gracht_server_register_protocol(gracht_protocol_t* protocol);

/**
 * Unregisters a previously registered protocol. Any messages targetted for that protocol will be ignored after
 * unregistering.
 * 
 * @param protocol The protocol that should be unregistered. 
 */
void gracht_server_unregister_protocol(gracht_protocol_t* protocol);

/**
 * Should only be used when main_loop is not used. This should be called every time a file-descriptor event
 * is invoked that the application did not handle itself. This dispatches the event if it matches any of the
 * descriptors that are owned by the server.
 * 
 * @param iod The server or client descriptor that an event occurred on.
 * @param events The type of events that have occured on the descriptor.
 * @return int Returns 0 if the event was handled by libgracht.
 */
int gracht_server_handle_event(int iod, unsigned int events);

/**
 * The server main loop function. This can be invoked if no additional handling is required by
 * the application. Currently this function does not return at any point. exit() should be called
 * to shutdown. It is not required to invoke the main_loop function, this is only a way to present
 * an easy way to run a server that has no additional logic.
 * 
 * @return int exit code of the application.
 */
int gracht_server_main_loop(void);

/**
 * Returns the datagram (connectionless) file descriptor that is used by the server.
 * 
 * @return int The datagram (connectionless) file descriptor.
 */
gracht_conn_t gracht_server_get_dgram_iod(void);

/**
 * Returns the epoll/select/completion port handle/descriptor that is used by the server.
 * 
 * @return aio_handle_t The handle/descriptor.
 */
gracht_handle_t gracht_server_get_set_iod(void);

/**
 * Invoked by protocol generated functions to respond to receieved messages.
 * 
 * @param context The context of the called function.
 * @param message The message format that is the reply.
 * @return int Result code of the send.
 */
int gracht_server_respond(struct gracht_recv_message* context, struct gracht_message* message);

/**
 * Invoked by protocol generated events to send events to specific clients.
 * 
 * @param client The client descriptor.
 * @param message The event message that should be sent.
 * @param flags The flags for the event.
 * @return int Result code of the send.
 */
int gracht_server_send_event(int client, struct gracht_message* message, unsigned int flags);

/**
 * Invoked by protocol generated events to broadcast events to all subscribed clients.
 * 
 * @param message The event message that should be sent.
 * @param flags The flags for the event.
 * @return int Result code of the broadcast.
 */
int gracht_server_broadcast_event(struct gracht_message* message, unsigned int flags);

#ifdef __cplusplus
}
#endif
#endif // !__GRACHT_SERVER_H__
