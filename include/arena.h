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
 * Gracht Arena Type Definitions & Structures
 * - This header describes the base arena-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_ARENA_H__
#define __GRACHT_ARENA_H__

#include "gracht/types.h"

struct gracht_arena;

/**
 * 
 * @param size 
 * @param arenaOut 
 * @return int 
 */
int gracht_arena_create(size_t size, struct gracht_arena** arenaOut);

/**
 * 
 * @param arena 
 */
void gracht_arena_destroy(struct gracht_arena* arena);

/**
 * 
 * @param arena 
 * @param allocation 
 * @param size 
 * @return void* 
 */
void* gracht_arena_allocate(struct gracht_arena* arena, void* allocation, size_t size);

/**
 * Partially or fully frees an allocation previously made by *_allocate. The size defines
 * how much of the previous allocation is freed, and is freed from the end of the allocation.
 * 
 * @param arena A pointer to the arena the allocation was made from
 * @param memory A pointer to the memory allocation.
 * @param size How much of the allocation should be freed. If the entire allocation should be freed size can also be 0.
 */
void gracht_arena_free(struct gracht_arena* arena, void* memory, size_t size);

#endif // !__GRACHT_ARENA_H__
