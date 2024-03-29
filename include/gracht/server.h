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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Gracht Server Type Definitions & Structures
 * - This header describes the base server-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_SERVER_H__
#define __GRACHT_SERVER_H__

#include "types.h"
#include "link/link.h"

struct gracht_server_callbacks {
    void (*clientConnected)(gracht_conn_t client);    // invoked only when a new stream-based client has connected
                                                      // or when a new connectionless-client has subscribed to the server
    void (*clientDisconnected)(gracht_conn_t client); // invoked only when a new stream-based client has disconnected
                                                      // or when a connectionless-client has unsubscribed from the server
};

typedef struct gracht_server_configuration {
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
    //                    to GRACHT_DEFAULT_MESSAGE_SIZE as the default value.
    int                            server_workers;
    int                            max_message_size;
} gracht_server_configuration_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Configuration interface, use these as helpers instead of accessing the raw structure.
 * When providing the aio descriptor to the server (windows: iocp handle, linux: epoll fd),
 * the server will then use that to queue up handles and listen for events. If the server is
 * used asynchronously, then the application must listen itself on the aio handle provided and
 * then call gracht_server_handle_event when it detects a handle from the server.
 */
GRACHTAPI void gracht_server_configuration_init(gracht_server_configuration_t* config);
GRACHTAPI void gracht_server_configuration_set_aio_descriptor(gracht_server_configuration_t* config, gracht_handle_t descriptor);
GRACHTAPI void gracht_server_configuration_set_num_workers(gracht_server_configuration_t* config, int workerCount);
GRACHTAPI void gracht_server_configuration_set_max_msg_size(gracht_server_configuration_t* config, int maxMessageSize);

/**
 * Creates a new instance of the gracht server instance based on the config provided. The configuratipn
 * must provide the link operations (see link.h) and can optionally provide a poll/select handle (set_descriptor)
 * or server callbacks.
 * 
 * @param config    The configuration structure that determines operation of the server.
 * @param serverOut A pointer to a gracht_server_t for storing the resulting server instance.
 * @return int      Result of the initialize, returns 0 for success.
 */
GRACHTAPI int gracht_server_create(gracht_server_configuration_t* config, gracht_server_t** serverOut);

/**
 * Requests shutdown of the server. The shutdown is not guaranteed to happen immediately, but rather
 * at the next request/event. This is highly dependant on how the server is used. If the server is
 * used as an asynchronous fashion (i.e through external epoll), then the cleanup will be performed
 * the next time gracht_server_handle_event is called.
 */
GRACHTAPI void gracht_server_request_shutdown(gracht_server_t* server);

/**
 * Registers a link with the server. The server can operate on multiple links, but it needs
 * atleast a single link to function. Any functions called without registering a link will
 * return errors. Gracht will automatically cleanup the link once the server shuts down.
 * 
 * @param link The link that should be registered
 * @return int Returns 0 if the protocol was link, or -1 if max count of links has been registered.
 */
GRACHTAPI int gracht_server_add_link(gracht_server_t* server, struct gracht_link* link);

/**
 * Registers a new protocol with the server. A max of 255 protocols can be registered, and if
 * the server is called with an unsupported protocol it ignores the message. Only messages that
 * match the protocol/function id pair is actually invoked.
 * 
 * @param protocol The protocol instance to register.
 * @return int Returns 0 if the protocol was registered, or -1 if max count of protocols has been registered.
 */
GRACHTAPI int gracht_server_register_protocol(gracht_server_t* server, gracht_protocol_t* protocol);

/**
 * Unregisters a previously registered protocol. Any messages targetted for that protocol will be ignored after
 * unregistering.
 * 
 * @param protocol The protocol that should be unregistered. 
 */
GRACHTAPI void gracht_server_unregister_protocol(gracht_server_t* server, gracht_protocol_t* protocol);

/**
 * Should only be used when main_loop is not used. This should be called every time a file-descriptor event
 * is invoked that the application did not handle itself. This dispatches the event if it matches any of the
 * descriptors that are owned by the server.
 * 
 * @param iod The server or client descriptor that an event occurred on.
 * @param events The type of events that have occured on the descriptor.
 * @return int Returns 0 if the event was handled by libgracht.
 */
GRACHTAPI int gracht_server_handle_event(gracht_server_t* server, gracht_conn_t handle, unsigned int events);

/**
 * The server main loop function. This can be invoked if no additional handling is required by
 * the application. Currently this function does not return at any point. exit() should be called
 * to shutdown. It is not required to invoke the main_loop function, this is only a way to present
 * an easy way to run a server that has no additional logic.
 * 
 * @return int exit code of the application.
 */
GRACHTAPI int gracht_server_main_loop(gracht_server_t* server);

/**
 * Returns the epoll/select/completion port handle/descriptor that is used by the server.
 * 
 * @return aio_handle_t The handle/descriptor.
 */
GRACHTAPI gracht_handle_t gracht_server_get_aio_handle(gracht_server_t* server);

/**
 * Creates a deferrable copy of a received message, allowing the caller to specify both
 * storage that must be of size GRACHT_MESSAGE_DEFERRABLE_SIZE, and also the message that
 * should be deffered. This must be done as messages are received in temporary buffers.
 * 
 */
GRACHTAPI void gracht_server_defer_message(struct gracht_message* in, struct gracht_message* out);

#ifdef __cplusplus
}
#endif
#endif // !__GRACHT_SERVER_H__
