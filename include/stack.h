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
 * Lockless stack implementation
 */

#ifndef __GRACHT_STACK_H__
#define __GRACHT_STACK_H__

#include "gatomic.h"
#include <stddef.h>

struct stack {
    atomic_uintptr_t elements;
    atomic_uint      index;
    size_t           count;
};

int   stack_construct(struct stack* stack, size_t initialCount);
void  stack_destroy(struct stack* stack);
void  stack_push(struct stack* stack, void* pointer);
void* stack_pop(struct stack* stack);

#endif //! __GRACHT_STACK_H__
