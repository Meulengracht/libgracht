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
 * Gracht Async IO Type Definitions & Structures
 * - This header describes the base aio-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_AIO_H__
#define __GRACHT_AIO_H__

#include "gracht/types.h"

#if defined(MOLLENOS)
#include <inet/socket.h>
#include <ioset.h>
#include <io.h>

typedef struct ioset_event gracht_aio_event_t;
#define GRACHT_AIO_EVENT_IN         IOSETIN
#define GRACHT_AIO_EVENT_DISCONNECT IOSETCTL

#define gracht_aio_create()                ioset(0)
#define gracht_io_wait(aio, events, count) ioset_wait(aio, events, count, 0)
#define gracht_aio_remove(aio, iod)        ioset_ctrl(aio, IOSET_DEL, iod, NULL);
#define gracht_aio_destroy(aio)            close(aio)

static int gracht_aio_add(int aio, int iod) {
    struct ioset_event event = {
        .events = IOSETIN | IOSETCTL | IOSETLVT,
        .data.iod = iod
    };
    return ioset_ctrl(aio, IOSET_ADD, iod, &event);
}

#define gracht_aio_event_handle(event)    (event)->data.iod
#define gracht_aio_event_events(event) (event)->events

#elif defined(__linux__)
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>

typedef struct epoll_event gracht_aio_event_t;
#define GRACHT_AIO_EVENT_IN         EPOLLIN
#define GRACHT_AIO_EVENT_DISCONNECT EPOLLRDHUP

#define gracht_aio_create()                epoll_create1(0)
#define gracht_io_wait(aio, events, count) epoll_wait(aio, events, count, -1);
#define gracht_aio_remove(aio, iod)        epoll_ctl(aio, EPOLL_CTL_DEL, iod, NULL)
#define gracht_aio_destroy(aio)            close(aio)

static int gracht_aio_add(int aio, int iod) {
    struct epoll_event event = {
        .events = EPOLLIN | EPOLLRDHUP,
        .data.fd = iod
    };
    return epoll_ctl(aio, EPOLL_CTL_ADD, iod, &event);
}

#define gracht_aio_event_handle(event)    (event)->data.fd
#define gracht_aio_event_events(event) (event)->events

#elif defined(_WIN32)
#include <windows.h>

typedef struct gracht_aio_win32_event {
    unsigned int events;
    unsigned int iod;
} gracht_aio_event_t;
#define GRACHT_AIO_EVENT_IN         0x1
#define GRACHT_AIO_EVENT_DISCONNECT 0x2

#define gracht_aio_create()            CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
#define gracht_aio_destroy(aio)        CloseHandle(aio)
#define gracht_aio_event_handle(event)    (event)->iod
#define gracht_aio_event_events(event) (event)->events

static int gracht_aio_add(aio_handle_t aio, unsigned int iod) {
    HANDLE handle = CreateIoCompletionPort((HANDLE)(uintptr_t)iod, aio, iod, 0);
    return handle == NULL ? -1 : 0;
}

static int gracht_aio_remove(aio_handle_t aio, unsigned int iod)
{
    // null op
    (void)aio;
    (void)iod;
    return 0;
}

static int gracht_io_wait(aio_handle_t aio, gracht_aio_event_t* events, int count)
{
    OVERLAPPED* overlapped      = NULL;
    DWORD       bytesTransfered = 0;
    void*       context         = NULL;
    BOOL        status          = GetQueuedCompletionStatus(aio,
        &bytesTransfered, (PULONG_PTR)&context, &overlapped, INFINITE);
    if (context == NULL) {
        return -1;
    }

    events[0].iod = (unsigned int)(uintptr_t)context;
    if (status == FALSE || (status == TRUE && bytesTransfered == 0)) {
        events[0].events = GRACHT_AIO_EVENT_DISCONNECT;
    }
    else {
        events[0].events = GRACHT_AIO_EVENT_IN;
    }
    return 1;
}

#else
#error "Undefined platform for aio"
#endif

#endif // !__GRACHT_AIO_H__
