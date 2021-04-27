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

typedef struct hashtable hashtable_t;

#ifdef _WIN32
#include <malloc.h>
#define alloca _alloca
#endif

#define GB_MSG_ID_0(buffer)  *((uint32_t*)(&((buffer)->data[0])))
#define GB_MSG_LEN_0(buffer) *((uint32_t*)(&((buffer)->data[4])))
#define GB_MSG_SID_0(buffer) *((uint8_t*)(&((buffer)->data[8])))
#define GB_MSG_AID_0(buffer) *((uint8_t*)(&((buffer)->data[9])))
#define GB_MSG_FLG_0(buffer) *((uint8_t*)(&((buffer)->data[10])))

#define GB_MSG_ID(buffer)  *((uint32_t*)(&((buffer)->data[(buffer)->index + 0])))
#define GB_MSG_LEN(buffer) *((uint32_t*)(&((buffer)->data[(buffer)->index + 4])))
#define GB_MSG_SID(buffer) *((uint8_t*)(&((buffer)->data[(buffer)->index + 8])))
#define GB_MSG_AID(buffer) *((uint8_t*)(&((buffer)->data[(buffer)->index + 9])))
#define GB_MSG_FLG(buffer) *((uint8_t*)(&((buffer)->data[(buffer)->index + 10])))

gracht_protocol_function_t* get_protocol_action(hashtable_t* protocols, uint8_t protocol_id, uint8_t action_id);

static uint64_t protocol_hash(const void* element)
{
    const struct gracht_protocol* protocol = element;
    return protocol->id;
}

static int protocol_cmp(const void* element1, const void* element2)
{
    const struct gracht_protocol* protocol1 = element1;
    const struct gracht_protocol* protocol2 = element2;
    return protocol1->id == protocol2->id ? 0 : 1;
}

#endif // !__GRACHT_UTILS_H__
