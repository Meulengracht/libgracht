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

#include "stream_pool_registry.h"
#include "buffer_pool.h"
#include "gracht/types.h"
#include <errno.h>
#include <stdlib.h>

size_t gracht_stream_normalize_buffer_size(size_t requestedSize, size_t fallbackSize)
{
    size_t size = requestedSize ? requestedSize : fallbackSize;

    if (!size) {
        size = GRACHT_DEFAULT_MESSAGE_SIZE;
    }

    return (size + (GRACHT_STREAM_BUFFER_ALIGNMENT - 1)) & ~(size_t)(GRACHT_STREAM_BUFFER_ALIGNMENT - 1);
}

void gracht_stream_pool_registry_destroy(struct gracht_stream_pool_registry* registry)
{
    size_t i;

    if (!registry) {
        return;
    }

    for (i = 0; i < registry->count; ++i) {
        gracht_buffer_pool_destroy(registry->entries[i].pool);
    }
    free(registry->entries);
    registry->entries = NULL;
    registry->count = 0;
    registry->capacity = 0;
}

struct gracht_buffer_pool* gracht_stream_pool_registry_get_or_create(
        struct gracht_stream_pool_registry* registry,
        size_t                              requestedSize,
        size_t                              bufferCount)
{
    struct gracht_buffer_pool* pool;
    struct gracht_stream_pool_entry* entries;
    size_t capacity;
    size_t i;

    if (!registry) {
        errno = EINVAL;
        return NULL;
    }

    for (i = 0; i < registry->count; ++i) {
        if (registry->entries[i].buffer_size == requestedSize) {
            return registry->entries[i].pool;
        }
    }

    if (registry->count == registry->capacity) {
        capacity = registry->capacity ? registry->capacity * 2 : 4;
        entries = realloc(registry->entries, sizeof(struct gracht_stream_pool_entry) * capacity);
        if (!entries) {
            errno = ENOMEM;
            return NULL;
        }
        registry->entries = entries;
        registry->capacity = capacity;
    }

    if (gracht_buffer_pool_create(requestedSize, bufferCount, &pool)) {
        return NULL;
    }

    registry->entries[registry->count].buffer_size = requestedSize;
    registry->entries[registry->count].pool = pool;
    registry->count++;
    return pool;
}

int gracht_stream_pool_registry_release(struct gracht_stream_pool_registry* registry, void* buffer)
{
    size_t i;

    if (!registry || !buffer) {
        return 0;
    }

    for (i = 0; i < registry->count; ++i) {
        if (gracht_buffer_pool_owns(registry->entries[i].pool, buffer)) {
            gracht_buffer_pool_release(registry->entries[i].pool, buffer);
            return 1;
        }
    }
    return 0;
}
