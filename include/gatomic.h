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
 * atomic operation definitions
 */

#ifndef __GRACHT_ATOMIC_H__
#define __GRACHT_ATOMIC_H__

#ifdef _WIN32
#include <stddef.h>
#include <stdint.h>
#include <windows.h>
#define ATOMIC_FLAG_INIT 0
#define ATOMIC_VAR_INIT(value) (value)
typedef intptr_t atomic_uint;
#define atomic_load(object) \
(MemoryBarrier(), *(object))
#define atomic_store(object, desired)   \
do {                                    \
    *(object) = (desired);              \
    MemoryBarrier();                    \
} while (0)

static inline int atomic_compare_exchange_strong(
    intptr_t *object, intptr_t *expected, intptr_t desired)
{
    intptr_t old = *expected;
    *expected = (intptr_t)InterlockedCompareExchangePointer(
        (PVOID *)object, (PVOID)desired, (PVOID)old);
    return *expected == old;
}

#ifdef _WIN64
#define atomic_fetch_add(object, operand) \
    InterlockedExchangeAdd(object, operand)

#define atomic_fetch_sub(object, operand) \
    InterlockedExchangeAdd(object, -(operand))
#else
#define atomic_fetch_add(object, operand) \
    InterlockedExchangeAdd64(object, operand)

#define atomic_fetch_sub(object, operand) \
    InterlockedExchangeAdd64(object, -(operand))
#endif
#else
#include <stdatomic.h>
#endif

#endif //! __GRACHT_ATOMIC_H__
