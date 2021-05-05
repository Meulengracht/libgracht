/**
 * Copyright 2020, Philip Meulengracht
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

#include <errno.h>
#include "stack.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
typedef uintptr_t uint_loaded_t;
#else
typedef unsigned int uint_loaded_t;
#endif

int stack_construct(struct stack* stack, size_t initialCount)
{
    uintptr_t space;
    if (!stack || !initialCount) {
        errno = EINVAL;
        return -1;
    }

    space = (uintptr_t)malloc(sizeof(uintptr_t) * initialCount);
    if (!space) {
        errno = ENOMEM;
        return -1;
    }

    stack->elements = ATOMIC_VAR_INIT(space);
    stack->index    = ATOMIC_VAR_INIT(0);
    stack->count    = initialCount;
    return 0;
}

void stack_destroy(struct stack* stack)
{
    if (!stack) {
        return;
    }

    free((void*)atomic_load(&stack->elements));
}

int stack_resize(struct stack* stack, size_t newSize)
{
    uintptr_t original = atomic_load(&stack->elements);
    uintptr_t resized  = (uintptr_t)malloc(sizeof(uintptr_t) * newSize);
    int       result;

    if (!resized) {
        errno = ENOMEM;
        return -1;
    }

    memcpy((void*)resized, (const void*)original, sizeof(uintptr_t) * stack->count);
    
    result = atomic_compare_exchange_strong(&stack->elements, &original, resized);
    if (!result) {
        // try again, someone else beat us to it
        free((void*)resized);
        return 0;
    }

    stack->count = newSize;
    return 0;
}

void stack_push(struct stack* stack, void* pointer)
{
    uint_loaded_t index;

    if (!stack || !pointer) {
        return;
    }

perform_push:
    index = atomic_fetch_add(&stack->index, 1);
    if (index >= stack->count) {
        if (stack_resize(stack, stack->count * 2)) {
            return;
        }
        goto perform_push;
    }

    ((uintptr_t*)atomic_load(&stack->elements))[index] = (uintptr_t)pointer;
}

void* stack_pop(struct stack* stack)
{
    uint_loaded_t index;
    int           result;

    if (!stack) {
        return NULL;
    }

perform_pop:
    index = atomic_load(&stack->index);
    if (!index) {
        return NULL;
    }

    result = atomic_compare_exchange_strong(&stack->index, &index, index - 1);
    if (!result) {
        goto perform_pop;
    }
    return (void*)((uintptr_t*)atomic_load(&stack->elements))[index - 1];
}
