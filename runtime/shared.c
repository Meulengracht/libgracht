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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Gracht Client Type Definitions & Structures
 * - This header describes the base client-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include "gracht/types.h"
#include "hashtable.h"
#include "logging.h"
#include "utils.h"
#include <errno.h>
#include <stdlib.h>

gracht_protocol_function_t* get_protocol_action(gr_hashtable_t* protocols,
    uint8_t protocol_id, uint8_t action_id)
{
    int                i;
    gracht_protocol_t* protocol;
    
    protocol = gr_hashtable_get(protocols, &(gracht_protocol_t) { .id = protocol_id });
    if (!protocol) {
        GRERROR(GRSTR("get_protocol_action(p=%u, a=%u) protocol was not implemented"), protocol_id, action_id);
        errno = ENOTSUP;
        return NULL;
    }
    
    for (i = 0; i < protocol->num_functions; i++) {
        if (protocol->functions[i].id == action_id) {
            return &protocol->functions[i];
        }
    }

    GRERROR(GRSTR("get_protocol_action(p=%u, a=%u) action was not implemented"), protocol_id, action_id);
    errno = ENOTSUP;
    return NULL;
}

gracht_conn_t gracht_link_get_handle(struct gracht_link* link)
{
    if (!link) {
        return GRACHT_CONN_INVALID;
    }

    return link->connection;
}

enum gracht_link_type gracht_link_get_type(struct gracht_link* link)
{
    if (!link) {
        return gracht_link_stream_based;
    }

    return link->type;
}

#if defined(GRACHT_SHARED_LIBRARY) && defined(MOLLENOS)
// dll entry point for mollenos shared libraries
// this must be present
void dllmain(int action) {
    (void)action;
}
#endif
