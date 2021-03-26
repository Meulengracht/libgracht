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
 * Gracht Socket Link Type Definitions & Structures
 * - This header describes the base link-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_SOCKET_OS_H__
#define __GRACHT_SOCKET_OS_H__

#if defined(_WIN32)
#include "../../include/utils.h"
#include <io.h>
#define i_iobuf_t  WSABUF
#define i_iobuf_set_buf(iobuf, base) (iobuf)->buf = (char*)(base);
#define i_iobuf_set_len(iobuf, _len) (iobuf)->len = (_len);
#define i_msghdr_t WSAMSG
#define I_MSGHDR_INIT { .name = NULL, .namelen = 0, .lpBuffers = NULL, .dwBufferCount = 0, .Control = { 0 }, .dwFlags = 0 }
#define i_msghdr_set_addr(msg, addr, len)   (msg)->name = (addr); (msg)->namelen = (len)
#define i_msghdr_set_bufs(msg, iobufs, cnt) (msg)->lpBuffers = (iobufs); (msg)->dwBufferCount = (cnt)
#define i_msghdr_addr_name(msg) ((msg)->name)
#define i_msghdr_addr_len(msg)  ((msg)->namelen)
#define i_msghdr_flags(msg)     ((msg)->dwFlags)
#define close closesocket

#define AF_LOCAL AF_INET

#define MSG_DONTWAIT 0x10000

static inline int sendmsg(int fd, const WSAMSG* message, int flags) {
    /*int WSAAPI WSASendMsg(
  SOCKET                             Handle,
  LPWSAMSG                           lpMsg,
  DWORD                              dwFlags,
  LPDWORD                            lpNumberOfBytesSent,
  LPWSAOVERLAPPED                    lpOverlapped,
  LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
);*/
    return -1;
}

static inline long recvmsg(int fd, WSAMSG* message, int flags) {
    return -1;
}
#else
#define i_iobuf_t  struct iovec
#define i_iobuf_set_buf(iobuf, base) (iobuf)->iov_base = (base);
#define i_iobuf_set_len(iobuf, len) (iobuf)->iov_len = (len);
#define i_msghdr_t struct msghdr
#define I_MSGHDR_INIT { .msg_name = NULL, .msg_namelen = 0, .msg_iov = NULL, .msg_iovlen = 0, .msg_control = NULL, .msg_controllen = 0, .msg_flags = 0 }
#define i_msghdr_set_addr(msg, addr, len)   (msg)->msg_name = (addr); (msg)->msg_namelen = (len)
#define i_msghdr_set_bufs(msg, iobufs, cnt) (msg)->msg_iov = (iobufs); (msg)->msg_iovlen = (cnt)
#define i_msghdr_addr_name(msg) ((msg)->msg_name)
#define i_msghdr_addr_len(msg)  ((msg)->msg_namelen)
#define i_msghdr_flags(msg)     ((msg)->msg_flags)
#endif

#endif // !__GRACHT_SOCKET_OS_H__
