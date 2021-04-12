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
 * Gracht Client Type Definitions & Structures
 * - This header describes the base client-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include "include/gracht/types.h"
#include "include/hashtable.h"
#include "include/debug.h"
#include "include/utils.h"
#include <errno.h>
#include <stdlib.h>

gracht_protocol_function_t* get_protocol_action(hashtable_t* protocols,
    uint8_t protocol_id, uint8_t action_id)
{
    int                i;
    gracht_protocol_t* protocol;
    
    protocol = hashtable_get(protocols, &(gracht_protocol_t) { .id = protocol_id });
    if (!protocol) {
        GRERROR(GRSTR("[get_protocol_action] protocol %u was not implemented"), protocol_id);
        errno = ENOTSUP;
        return NULL;
    }
    
    for (i = 0; i < protocol->num_functions; i++) {
        if (protocol->functions[i].id == action_id) {
            return &protocol->functions[i];
        }
    }

    GRERROR(GRSTR("[get_protocol_action] action %u was not implemented"), action_id);
    errno = ENOTSUP;
    return NULL;
}

void unpack_parameters(struct gracht_param* params, uint8_t count, void* params_storage, uint8_t* unpackBuffer)
{
    char* pointer = params_storage;
    int   unpackIndex    = 0;
    int   i;

    for (i = 0; i < count; i++) {
        if (params[i].type == GRACHT_PARAM_VALUE) {
            GRTRACE(GRSTR("push value: %u"), (uint32_t)(params[i].data.value & 0xFFFFFFFF));
            if (params[i].length == 1) {
                unpackBuffer[unpackIndex] = (uint8_t)(params[i].data.value & 0xFF);
            }
            else if (params[i].length == 2) {
                *((uint16_t*)&unpackBuffer[unpackIndex]) = (uint16_t)(params[i].data.value & 0xFFFF);
            }
            else if (params[i].length == 4) {
                *((uint32_t*)&unpackBuffer[unpackIndex]) = (uint32_t)(params[i].data.value & 0xFFFFFFFF);
            }
            else if (params[i].length == 8) {
#if defined(amd64) || defined(__amd64__)
                *((uint64_t*)&unpackBuffer[unpackIndex]) = params[i].data.value;
#else
                memcpy(&unpackBuffer[unpackIndex], &params[i].data.bytes[0], 8);
#endif
            }
            unpackIndex += params[i].length;
        }
        else if (params[i].type == GRACHT_PARAM_BUFFER) {
            GRTRACE(GRSTR("push pointer: 0x%p %u"), pointer, params[i].length);
            if (params[i].length == 0) {
                *((char**)&unpackBuffer[unpackIndex]) = NULL;
            }
            else {
                *((char**)&unpackBuffer[unpackIndex]) = pointer;
                pointer += params[i].length;
            }
            unpackIndex += sizeof(void*);
        }
        else if (params[i].type == GRACHT_PARAM_SHM) {
            *((char**)&unpackBuffer[unpackIndex]) = (char*)params[i].data.buffer;
            unpackIndex += sizeof(void*);
        }
    }
}
