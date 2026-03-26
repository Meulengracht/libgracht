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
 * Stream pool registry - manages multiple buffer pools keyed by size class.
 */

#ifndef __GRACHT_STREAM_POOL_REGISTRY_H__
#define __GRACHT_STREAM_POOL_REGISTRY_H__

#include <stddef.h>

#define GRACHT_STREAM_BUFFER_ALIGNMENT 256u

struct gracht_buffer_pool;

struct gracht_stream_pool_entry {
    size_t                     buffer_size;
    struct gracht_buffer_pool* pool;
};

struct gracht_stream_pool_registry {
    struct gracht_stream_pool_entry* entries;
    size_t                           count;
    size_t                           capacity;
};

/**
 * Rounds a requested size up to the nearest GRACHT_STREAM_BUFFER_ALIGNMENT
 * boundary. Falls back to fallbackSize (then GRACHT_DEFAULT_MESSAGE_SIZE)
 * when requestedSize is 0.
 */
size_t gracht_stream_normalize_buffer_size(size_t requestedSize, size_t fallbackSize);

/**
 * Destroys all pools in the registry and frees the entry array.
 */
void gracht_stream_pool_registry_destroy(struct gracht_stream_pool_registry* registry);

/**
 * Looks up or lazily creates a buffer pool for the given size class.
 * Returns the pool, or NULL on error (errno set).
 */
struct gracht_buffer_pool* gracht_stream_pool_registry_get_or_create(
        struct gracht_stream_pool_registry* registry,
        size_t                              requestedSize,
        size_t                              bufferCount);

/**
 * Releases a buffer back to whichever pool owns it.
 * Returns 1 if a pool claimed the buffer, 0 otherwise.
 */
int gracht_stream_pool_registry_release(struct gracht_stream_pool_registry* registry, void* buffer);

#endif /* __GRACHT_STREAM_POOL_REGISTRY_H__ */
