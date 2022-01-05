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
 * Reader/Writer mutex implementation
 * It is a rather crude implementation of a read-write mutex that uses
 * a reader counter and a mutex/condition for synchronization.
 */

#ifndef __GRACHT_RWLOCK_H__
#define __GRACHT_RWLOCK_H__

#include "thread_api.h"
#include <assert.h>

// taken from my other project vioarr
struct rwlock {
    mtx_t sync_object;
    int   readers;
    cnd_t signal;
};

static void rwlock_init(struct rwlock* lock)
{
    mtx_init(&lock->sync_object, mtx_plain);
    cnd_init(&lock->signal);
    lock->readers = 0;
}

static void rwlock_destroy(struct rwlock* lock)
{
    cnd_destroy(&lock->signal);
    mtx_destroy(&lock->sync_object);
}

static void rwlock_r_lock(struct rwlock* lock)
{
    mtx_lock(&lock->sync_object);
    lock->readers++;
    mtx_unlock(&lock->sync_object);
}

static void rwlock_r_unlock(struct rwlock* lock)
{
    mtx_lock(&lock->sync_object);
    assert(lock->readers);
    lock->readers--;
    if (!lock->readers) {
        cnd_signal(&lock->signal);
    }
    mtx_unlock(&lock->sync_object);
}

static void rwlock_w_lock(struct rwlock* lock)
{
    mtx_lock(&lock->sync_object);
    while (lock->readers) {
        cnd_wait(&lock->signal, &lock->sync_object);
    }
}

static void rwlock_w_unlock(struct rwlock* lock)
{
    mtx_unlock(&lock->sync_object);
    cnd_signal(&lock->signal);
}

#endif //! __GRACHT_RWLOCK_H__

