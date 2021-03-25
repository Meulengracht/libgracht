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

// forward declarations
struct gracht_server;
struct gracht_arena;
struct gracht_worker_pool;

// server callbacks
typedef void (*server_invoke00_t)(struct gracht_recv_message*);
typedef void (*server_invokeA0_t)(struct gracht_recv_message*, void*);

/**
 * Defined in arena.c
 * 
 * @param size 
 * @param arenaOut 
 * @return int 
 */
int gracht_arena_create(size_t size, struct gracht_arena** arenaOut);

/**
 * Defined in arena.c
 * 
 * @param arena 
 */
void gracht_arena_destroy(struct gracht_arena* arena);

/**
 * Defined in arena.c
 * 
 * @param arena 
 * @param allocation 
 * @param size 
 * @return void* 
 */
void* gracht_arena_allocate(struct gracht_arena* arena, void* allocation, size_t size);

/**
 * Defined in arena.c
 * Partially or fully frees an allocation previously made by *_allocate. The size defines
 * how much of the previous allocation is freed, and is freed from the end of the allocation.
 * 
 * @param arena A pointer to the arena the allocation was made from
 * @param memory A pointer to the memory allocation.
 * @param size How much of the allocation should be freed. If the entire allocation should be freed size can also be 0.
 */
void gracht_arena_free(struct gracht_arena* arena, void* memory, size_t size);

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
int gracht_worker_pool_create(struct gracht_server* server, int numberOfWorkers, struct gracht_worker_pool** poolOut);

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
 * Defined in server.c
 * Finds and executes the correct callback based on the message information and the protocols provided.
 * 
 * @param server A pointer to the server instance
 * @param recvMessage A pointer to the recv_message structure that contains message data.
 * @return int Returns 0 if the callback was successfully found.
 */
int server_invoke_action(struct gracht_server* server, struct gracht_recv_message* recvMessage);

/**
 * Defined in server.c
 * Callback to server to notify that the message is now free for cleanup. Called by workers.
 * 
 * @param server A pointer to the server the messages originates on.
 * @param recvMessage A pointer to the message structure.
 */
void server_cleanup_message(struct gracht_server* server, struct gracht_recv_message* recvMessage);

#endif // !__SERVER_PRIVATE_H__
