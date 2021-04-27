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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Gracht Server Type Definitions & Structures
 * - This header describes the base server-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __SERVER_PRIVATE_H__
#define __SERVER_PRIVATE_H__

#include "gracht/types.h"
#include "queue.h"

// forward declarations
struct gracht_server;
struct gracht_worker_pool;

// Callback prototype
typedef void (*server_invoke_t)(struct gracht_recv_message*, struct gracht_buffer*);

/**
 * Defined in dispatch.c
 * Creates a new threadpool with the specified number of workers. This can then be used to dispatch messages
 * in effecient and high-speed fashion.
 * 
 * @param server
 * @param numberOfWorkers The number of workers that should be in the pool
 * @param poolOut A pointer to storage for the worker pool.
 * @return int Returns 0 if creation was succesfull, otherwise errno is set.
 */
int gracht_worker_pool_create(struct gracht_server* server, int numberOfWorkers, int maxMessageSize, struct gracht_worker_pool** poolOut);

/**
 * Defined in dispatch.c
 * Destroys the threadpool, handles shutdown of all workers and cleans up resources. Any outstanding messages
 * will be destroyed.
 * 
 * @param pool A pointer to the worker pool that was created earlier.
 */
void gracht_worker_pool_destroy(struct gracht_worker_pool* pool);

/**
 * Defined in dispatch.c
 * Dispatches the recieved message to a ready worker.
 * 
 * @param pool A pointer to the worker pool that was created earlier.
 * @param recvMessage A pointer to the recieved message.
 */
void gracht_worker_pool_dispatch(struct gracht_worker_pool* pool, struct gracht_recv_message* recvMessage);

/**
 * Retrieves a buffer area that is free to be used for the calling thread. In the context of
 * the gracht server this scratch area is used to build an outoing message.
 * 
 * @param pool A pointer to the worker pool that was created earlier.
 * @return void* A pointer to the scratchpad area
 */
void* gracht_worker_pool_get_worker_scratchpad(struct gracht_worker_pool* pool);

/**
 * Defined in server.c
 * Finds and executes the correct callback based on the message information and the protocols provided.
 * 
 * @param server A pointer to the server instance
 * @param recvMessage A pointer to the recv_message structure that contains message data.
 */
void server_invoke_action(struct gracht_server* server, struct gracht_recv_message* recvMessage);

/**
 * Defined in server.c
 * Callback to server to notify that the message is now free for cleanup. Called by workers.
 * 
 * @param server A pointer to the server the messages originates on.
 * @param recvMessage A pointer to the message structure.
 */
void server_cleanup_message(struct gracht_server* server, struct gracht_recv_message* recvMessage);

#endif // !__SERVER_PRIVATE_H__
