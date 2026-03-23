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

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include "buffer_pool.h"
#include "stack.h"

struct gracht_buffer_pool {
    struct stack free_buffers;
    void*        storage;
    int          owns_storage;
    size_t       buffer_size;
    size_t       buffer_count;
};

static int gracht_buffer_pool_create_internal(
        size_t                     bufferSize,
        size_t                     bufferCount,
        void*                      storage,
        int                        ownsStorage,
        struct gracht_buffer_pool** poolOut)
{
    struct gracht_buffer_pool* pool;
    uint8_t*                   base;

    if (!bufferSize || !bufferCount || !poolOut) {
        errno = EINVAL;
        return -1;
    }

    pool = malloc(sizeof(struct gracht_buffer_pool));
    if (!pool) {
        errno = ENOMEM;
        return -1;
    }

    pool->storage = storage;
    pool->owns_storage = ownsStorage;
    pool->buffer_size = bufferSize;
    pool->buffer_count = bufferCount;
    if (!pool->storage) {
        pool->storage = malloc(bufferSize * bufferCount);
        if (!pool->storage) {
            free(pool);
            errno = ENOMEM;
            return -1;
        }
        pool->owns_storage = 1;
    }

    if (stack_construct(&pool->free_buffers, bufferCount)) {
        if (pool->owns_storage) {
            free(pool->storage);
        }
        free(pool);
        return -1;
    }

    base = pool->storage;
    for (size_t i = 0; i < bufferCount; i++) {
        stack_push(&pool->free_buffers, &base[i * bufferSize]);
    }

    *poolOut = pool;
    return 0;
}

int gracht_buffer_pool_create(size_t bufferSize, size_t bufferCount, struct gracht_buffer_pool** poolOut)
{
    return gracht_buffer_pool_create_internal(bufferSize, bufferCount, NULL, 1, poolOut);
}

int gracht_buffer_pool_create_with_storage(
        size_t                     bufferSize,
        size_t                     bufferCount,
        void*                      storage,
        struct gracht_buffer_pool** poolOut)
{
    if (!storage) {
        errno = EINVAL;
        return -1;
    }
    return gracht_buffer_pool_create_internal(bufferSize, bufferCount, storage, 0, poolOut);
}

void gracht_buffer_pool_destroy(struct gracht_buffer_pool* pool)
{
    if (!pool) {
        return;
    }

    stack_destroy(&pool->free_buffers);
    if (pool->owns_storage) {
        free(pool->storage);
    }
    free(pool);
}

void* gracht_buffer_pool_acquire(struct gracht_buffer_pool* pool)
{
    if (!pool) {
        return NULL;
    }
    return stack_pop(&pool->free_buffers);
}

void gracht_buffer_pool_release(struct gracht_buffer_pool* pool, void* buffer)
{
    if (!pool || !buffer) {
        return;
    }
    stack_push(&pool->free_buffers, buffer);
}

int gracht_buffer_pool_owns(struct gracht_buffer_pool* pool, void* buffer)
{
    uint8_t* start;
    uint8_t* end;
    uint8_t* ptr;

    if (!pool || !buffer || !pool->storage) {
        return 0;
    }

    start = pool->storage;
    end = start + (pool->buffer_size * pool->buffer_count);
    ptr = buffer;
    return ptr >= start && ptr < end;
}