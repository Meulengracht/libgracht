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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Fixed-size buffer pool implementation.
 */

#ifndef __GRACHT_BUFFER_POOL_H__
#define __GRACHT_BUFFER_POOL_H__

#include <stddef.h>

struct gracht_buffer_pool;

int   gracht_buffer_pool_create(size_t bufferSize, size_t bufferCount, struct gracht_buffer_pool** poolOut);
int   gracht_buffer_pool_create_with_storage(size_t bufferSize, size_t bufferCount, void* storage, struct gracht_buffer_pool** poolOut);
void  gracht_buffer_pool_destroy(struct gracht_buffer_pool* pool);
void* gracht_buffer_pool_acquire(struct gracht_buffer_pool* pool);
void  gracht_buffer_pool_release(struct gracht_buffer_pool* pool, void* buffer);

#endif //! __GRACHT_BUFFER_POOL_H__