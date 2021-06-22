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
 * Gracht Link Type Definitions & Structures
 * - This header describes the base link-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_LINK_H__
#define __GRACHT_LINK_H__

#include "../types.h"

// Supported link types that the server and client can communicate
// The stream based link means that the client tries to connect in TCP-mode
// The packet based link means that the client tries to connect in UDP-mode
enum gracht_link_type {
    gracht_link_stream_based, // connection mode
    gracht_link_packet_based  // connection less mode
};

// Represents a client from the server point of view, and will be given when trying
// to communicate with the client. The link functions will have this information available.
struct gracht_server_client {
    gracht_conn_t handle;
    uint32_t      flags;
    uint32_t      subscriptions[8]; // 32 bytes to cover 255 bits
};

// forward declares
struct gracht_link;

// Server link API callbacks.
typedef int (*server_accept_client_fn)(struct gracht_link*, gracht_handle_t set_handle, struct gracht_server_client**);
typedef int (*server_create_client_fn)(struct gracht_link*, struct gracht_message*, struct gracht_server_client**);
typedef int (*server_destroy_client_fn)(struct gracht_server_client*, gracht_handle_t set_handle);
typedef int (*server_recv_client_fn)(struct gracht_server_client*, struct gracht_message*, unsigned int flags);
typedef int (*server_send_client_fn)(struct gracht_server_client*, struct gracht_buffer*, unsigned int flags);

typedef int (*server_link_recv_fn)(struct gracht_link*, struct gracht_message*, unsigned int flags);
typedef int (*server_link_send_fn)(struct gracht_link*, struct gracht_message*, struct gracht_buffer*);

typedef gracht_conn_t (*server_link_setup_fn)(struct gracht_link*, gracht_handle_t set_handle);
typedef void          (*server_link_destroy_fn)(struct gracht_link*, gracht_handle_t set_handle);

struct server_link_ops {
    /**
     * Connection oriented functions, and not something that must be supported by
     * links. These are only for streaming links that support clients.
     */
    server_accept_client_fn  accept_client;
    server_create_client_fn  create_client;
    server_destroy_client_fn destroy_client;
    server_recv_client_fn    recv_client;
    server_send_client_fn    send_client;

    /**
     * Connection-less oriented functions, and must be supported by the link
     * if the link-type is packet.
     */
    server_link_recv_fn recv;
    server_link_send_fn send;
    
    /**
     * Shared functions that must be implemented for links.
     */
    server_link_setup_fn       setup;
    server_link_destroy_fn     destroy;
};

// Client link API callbacks.
typedef gracht_conn_t (*client_link_connect_fn)(struct gracht_link*);
typedef int           (*client_link_recv_fn)(struct gracht_link*, struct gracht_buffer*, unsigned int flags);
typedef int           (*client_link_send_fn)(struct gracht_link*, struct gracht_buffer*, void* messageContext);
typedef void          (*client_link_destroy_fn)(struct gracht_link*);

struct client_link_ops {
    client_link_connect_fn connect;
    client_link_recv_fn    recv;
    client_link_send_fn    send;
    client_link_destroy_fn destroy;
};

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Link interface that can be used to query data from links if the application
 * keeps the link pointer around.
 */
GRACHTAPI gracht_conn_t         gracht_link_get_handle(struct gracht_link* link);
GRACHTAPI enum gracht_link_type gracht_link_get_type(struct gracht_link* link);

#ifdef __cplusplus
}
#endif

#endif // !__GRACHT_LINK_H__
