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
 * Gracht Utils Type Definitions & Structures
 * - This header describes the base utils-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_UTILS_H__
#define __GRACHT_UTILS_H__

#include "gracht/types.h"

gracht_protocol_function_t* get_protocol_action(struct gracht_list* protocols, uint8_t protocol_id, uint8_t action_id);
void unpack_parameters(struct gracht_param* params, uint8_t count, void* params_storage, uint8_t* unpackBuffer);

#endif // !__GRACHT_UTILS_H__
