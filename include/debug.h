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
 * Gracht Debug Type Definitions & Structures
 * - This header describes the base debug-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_DEBUG_H__
#define __GRACHT_DEBUG_H__

//#define GRACHT_TRACE

// define format specifiers that vary from os to os
#ifdef _WIN32
#define F_CONN_T "zu"
#else
#define F_CONN_T "i"
#endif

#if defined(MOLLENOS)
//#define __TRACE
#include <ddk/utils.h>

#define GRSTR(str) str
#define GRTRACE    TRACE
#define GRWARNING  WARNING
#define GRERROR    ERROR
#else

#include <stdio.h>
#define GRSTR(str)     str "\n"
#define GRTRACE(...)   printf(__VA_ARGS__)
#define GRWARNING(...) printf(__VA_ARGS__)
#define GRERROR(...)   printf(__VA_ARGS__)
#endif

#ifndef GRACHT_TRACE
#undef GRTRACE
#define GRTRACE(...)
#endif

#endif // !__GRACHT_DEBUG_H__
