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
 *
 * Memory Arenas implementation
 *  - It is a rather crude implementation, and is in severe needs of a bit of
 *    optimizations. Essentially what we are trying to provide is quick allocation
 *    of resizable buffers. It keeps allocations as "linked" lists by providing the
 *    ability to calculate the position of the next allocation header based on the
 *    current location of the initial header
 */

#include <errno.h>
#include "thread_api.h"
#include "logging.h"
#include "server_private.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct gracht_header {
    uint32_t length    : 24;
    uint32_t allocated : 1;
    uint32_t flags     : 7;
    uint32_t payload[1];
};

struct gracht_arena {
    mtx_t                 mutex;
    struct gracht_header* base;
    size_t                length;
};

// for our purposes we require atleast 128 bytes for a new message
#define ALLOCATION_SPILLOVER_THRESHOLD 128

#define HEADER_SIZE          (sizeof(struct gracht_header) - sizeof(uint32_t))
#define GET_HEADER(ptr)      ((struct gracht_header*)((char*)(ptr) - HEADER_SIZE))
#define GET_NEXT_HEADER(hdr) ((struct gracht_header*)((char*)(hdr) + (HEADER_SIZE + (hdr)->length)))

void gracht_arena_free(struct gracht_arena* arena, void* memory, size_t size);

int gracht_arena_create(size_t size, struct gracht_arena** arenaOut)
{
    struct gracht_arena* arena;
    void*                base;

    if (!size || !arenaOut) {
        errno = EINVAL;
        return -1;
    }

    base = malloc(size);
    if (!base) {
        errno = ENOMEM;
        return -1;
    }

    arena = malloc(sizeof(struct gracht_arena));
    if (!arena) {
        free(base);
        errno = ENOMEM;
        return -1;
    }

    mtx_init(&arena->mutex, mtx_recursive);
    arena->base = base;
    arena->length = size;

    // initialize the first header
    arena->base->length = (uint32_t)(size & 0x00FFFFFF) - HEADER_SIZE;
    arena->base->flags  = 0;
    arena->base->allocated = 0;

    *arenaOut = arena;
    return 0;
}

void gracht_arena_destroy(struct gracht_arena* arena)
{
    if (!arena) {
        return;
    }

    mtx_destroy(&arena->mutex);
    free(arena->base);
    free(arena);
}

static inline void create_header(void* memory, uint32_t size)
{
    GRTRACE(GRSTR("create_header(memory=0x%p, size=%u)"), memory, size);
    struct gracht_header* header = memory;
    header->length = size - HEADER_SIZE;
    header->allocated = 0;
    header->flags = 0;
}

static inline void move_header(struct gracht_header* header, long byteCount)
{
    void* target = ((char*)header + byteCount);
    GRTRACE(GRSTR("moving header from %p to %p"), header, target);

    memmove(target, header, HEADER_SIZE);
}

static inline struct gracht_header* find_free_header(struct gracht_arena* arena, uint32_t size)
{
    struct gracht_header* itr    = arena->base;
    uint32_t              length = 0;

    GRTRACE(GRSTR("find_free_header(arena=0x%p, size=%u)"), arena, size);
    while (length < arena->length) {
        GRTRACE(GRSTR("header: at=0x%p length=%u, allocated=%i"), itr, itr->length, itr->allocated);
        if (!itr->allocated && itr->length >= size) {
            return itr;
        }

        if (!itr->length) {
            break;
        }

        length += itr->length;
        itr = GET_NEXT_HEADER(itr);
    }
    return NULL;
}

void* gracht_arena_allocate(struct gracht_arena* arena, void* allocation, size_t size)
{
    struct gracht_header* allocHeader;
    uint32_t              correctedSize = (uint32_t)(size & 0x00FFFFFF);
    GRTRACE(GRSTR("gracht_arena_allocate(arena=0x%p, allocation=0x%p, size=%u)"), arena, allocation, correctedSize);

    if (!arena || !size) {
        return NULL;
    }

    mtx_lock(&arena->mutex);
    if (allocation) {
        struct gracht_header* header     = GET_HEADER(allocation);
        struct gracht_header* nextHeader = GET_NEXT_HEADER(header);

        if (!nextHeader || nextHeader->allocated) {
            // we must reallocate the memory space to somewhere else
            correctedSize += header->length;

            allocHeader = find_free_header(arena, correctedSize);
            memcpy(&allocHeader->payload[0], &header->payload[0], header->length);

            // what consequences could this call have
            gracht_arena_free(arena, allocation, header->length);
        }
        else {
            // we are able to safely extend the current allocation
            header->length += correctedSize;
            nextHeader->length -= correctedSize;
            move_header(nextHeader, (long)correctedSize);
            mtx_unlock(&arena->mutex);
            return &header->payload[0];
        }
    }
    else {
        allocHeader = find_free_header(arena, correctedSize);
    }
    
    if (!allocHeader) {
        mtx_unlock(&arena->mutex);
        return NULL;
    }

    // if the bytes left in the header is less than a threshold, then we consolidate
    // with the current allocation.
    if ((allocHeader->length - correctedSize) < ALLOCATION_SPILLOVER_THRESHOLD) {
        correctedSize = allocHeader->length;
    }

    // create a new free header
    if (correctedSize != allocHeader->length) {
        create_header(((char*)allocHeader + HEADER_SIZE + correctedSize), allocHeader->length - correctedSize);
    }

    // update current header
    allocHeader->allocated = 1;
    allocHeader->length = (uint32_t)(correctedSize & 0x00FFFFFF);
    GRTRACE(GRSTR("gracht_arena_allocate returns=%p"), &allocHeader->payload[0]);
    mtx_unlock(&arena->mutex);
    return &allocHeader->payload[0];
}

void gracht_arena_free(struct gracht_arena* arena, void* memory, size_t size)
{
    struct gracht_header* header;
    struct gracht_header* nextHeader;
    uint32_t              allocLength = (uint32_t)(size & 0x00FFFFFF);
    GRTRACE(GRSTR("gracht_arena_free(arena=0x%p, memory=0x%p, size=%u)"), arena, memory, allocLength);

    if (!arena || !memory) {
        return;
    }

    mtx_lock(&arena->mutex);
    header     = GET_HEADER(memory);
    nextHeader = GET_NEXT_HEADER(header);

    // currently we only merge in forward direction, to merge in backwards
    // direction without iterating we need to add an allocation footer
    if (allocLength != 0 && header->length != allocLength) {
        // must free atleast enough, otherwise skip partial-free operation
        if (allocLength < sizeof(struct gracht_header)) {
            mtx_unlock(&arena->mutex);
            return;
        }

        // reduce the size of this allocation by the number of bytes we free
        GRTRACE(GRSTR("%p=reducing allocation from %u by %u"), header, header->length, allocLength);
        header->length -= allocLength;

        // either we must adjust the header link or create a new
        // based on whether its free or not
        if (!nextHeader->allocated) {
            long negated = 0 - (long)allocLength;
            GRTRACE(GRSTR("%p=moving by %li bytes"), nextHeader, negated);

            // header is not allocated, we add the size to it and move it
            GRTRACE(GRSTR("%p=increasing length by %u bytes to %u bytes"), nextHeader, allocLength, nextHeader->length);
            nextHeader->length += allocLength;
            move_header(nextHeader, negated);
        } else {
            // it was allocated, we must create a new header with the size that was freed
            create_header(GET_NEXT_HEADER(header), allocLength);
        }
    }
    else {
        header->allocated = 0;
        if (!nextHeader->allocated) {
            header->length += nextHeader->length + HEADER_SIZE;
        }
    }
    mtx_unlock(&arena->mutex);
}

//#define __TEST
#ifdef __TEST

int main()
{
    struct gracht_arena* arena;
    void* alloc0, *alloc1, *alloc2, *alloc3;

    GRTRACE(GRSTR("gracht_arena_create(10000, &arena) = %i"), gracht_arena_create(10000, &arena));

    alloc0 = gracht_arena_allocate(arena, NULL, 512);
    GRTRACE(GRSTR("gracht_arena_allocate(arena, NULL, 512) = 0x%p"), alloc0);

    alloc1 = gracht_arena_allocate(arena, NULL, 512);
    GRTRACE(GRSTR("gracht_arena_allocate(arena, NULL, 512) = 0x%p"), alloc1);

    alloc0 = gracht_arena_allocate(arena, alloc0, 512);
    GRTRACE(GRSTR("gracht_arena_allocate(arena, alloc0, 512) = 0x%p"), alloc0);

    alloc2 = gracht_arena_allocate(arena, NULL, 512);
    GRTRACE(GRSTR("gracht_arena_allocate(arena, NULL, 512) = 0x%p"), alloc2);

    gracht_arena_free(arena, alloc2, 128);
    gracht_arena_free(arena, alloc2, 128);

    alloc3 = gracht_arena_allocate(arena, NULL, 512);
    GRTRACE(GRSTR("gracht_arena_allocate(arena, NULL, 512) = 0x%p"), alloc3);
    
    gracht_arena_free(arena, alloc2, 128);
    gracht_arena_free(arena, alloc2, 128);
    gracht_arena_free(arena, alloc0, 1024);
    gracht_arena_free(arena, alloc1, 512);
    gracht_arena_free(arena, alloc3, 0);

    gracht_arena_destroy(arena);
}

#endif
