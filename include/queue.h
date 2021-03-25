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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Gracht List Type Definitions & Structures
 * - This header describes the base list-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_LIST_H__
#define __GRACHT_LIST_H__

#include "gracht/types.h"

typedef struct gracht_queue {
    struct gracht_object_header* head;
    struct gracht_object_header* tail;
} gracht_queue_t;

static void
gracht_queue_queue(struct gracht_queue* queue, struct gracht_object_header* item)
{
    if (!queue->tail) {
        queue->head = item;
        queue->tail = item;
    }
    else {
        queue->tail->link = item;
        queue->tail = item;
    }
}

static struct gracht_object_header*
gracht_queue_dequeue(struct gracht_queue* queue)
{
    struct gracht_object_header* item;

    if (!queue || !queue->head) {
        return NULL;
    }

    item = queue->head;
    queue->head = item->link;
    if (!queue->head) {
        queue->tail = NULL;
    }
    return item;
}

#endif // !__GRACHT_LIST_H__
