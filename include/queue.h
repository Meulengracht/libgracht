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
 * Gracht Queue Type Definitions & Structures
 * - This header describes the base queue-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_QUEUE_H__
#define __GRACHT_QUEUE_H__

#include <stdint.h>

struct gr_queue {
    unsigned int dequeue_index;
    unsigned int queue_index;
    unsigned int capacity;
    uintptr_t*   elements;
};

int   gr_queue_construct(struct gr_queue* queue, unsigned int capacity);
void  gr_queue_destroy(struct gr_queue* queue);
int   gr_queue_enqueue(struct gr_queue* queue, void* pointer);
void* gr_queue_dequeue(struct gr_queue* queue);

#endif // !__GRACHT_QUEUE_H__
