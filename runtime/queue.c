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
 * Generic queue implementation, since these operations will be performed
 * during lock of a mutex, no sync is taken into account here.
 */

#include <errno.h>
#include "queue.h"
#include <stdlib.h>

int gr_queue_construct(struct gr_queue* queue, unsigned int capacity)
{
    if (!queue || !capacity) {
        errno = EINVAL;
        return -1;
    }


    queue->elements = malloc(sizeof(uintptr_t) * capacity);
    if (!queue->elements) {
        errno = ENOMEM;
        return -1;
    }

    queue->capacity      = capacity;
    queue->dequeue_index = 0;
    queue->queue_index   = 0;
    return 0;
}

void gr_queue_destroy(struct gr_queue* queue)
{
    if (!queue) {
        return;
    }

    free(queue->elements);
}

static inline int capacity_remaining(struct gr_queue* queue)
{
    // return the remaining capacity, which is the current number of queued elements
    // subtracted from the capacity of the queue
    return queue->capacity - (queue->queue_index - queue->dequeue_index);
}

int gr_queue_enqueue(struct gr_queue* queue, void* pointer)
{
    unsigned int index;

    if (!queue || !pointer) {
        errno = EINVAL;
        return -1;
    }

    if (!capacity_remaining(queue)) {
        errno = ENOENT;
        return -1;
    }

    index = (queue->queue_index++) % queue->capacity;
    queue->elements[index] = (uintptr_t)pointer;
    return 0;
}

void* gr_queue_dequeue(struct gr_queue* queue)
{
    unsigned int index;

    if (!queue) {
        errno = EINVAL;
        return NULL;
    }

    if (queue->dequeue_index == queue->queue_index) {
        errno = ENOENT;
        return NULL;
    }

    index = (queue->dequeue_index++) % queue->capacity;    
    return (void*)queue->elements[index];
}
