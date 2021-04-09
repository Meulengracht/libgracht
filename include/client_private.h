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
 * Gracht Client Type Definitions & Structures
 * - This header describes the base server-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __CLIENT_PRIVATE_H__
#define __CLIENT_PRIVATE_H__

#include "gracht/types.h"

// forward declarations
struct gracht_client;

// Callback prototype
typedef void (*client_invoke00_t)(struct gracht_client*);
typedef void (*client_invokeA0_t)(struct gracht_client*, void*);

#endif // !__CLIENT_PRIVATE_H__
