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
 *
 * Gracht Utils Type Definitions & Structures
 * - This header describes the base utils-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_UTILS_H__
#define __GRACHT_UTILS_H__

#include "gracht/types.h"
#include "gracht/link/link.h"
#include "gracht/codec.h"

typedef struct gr_hashtable gr_hashtable_t;

#ifdef _WIN32
#include <malloc.h>
#define alloca _alloca
#endif

struct gracht_link {
    enum gracht_link_type type;
    union {
        struct server_link_ops server;
        struct client_link_ops client;
    } ops;
    gracht_conn_t connection;
};

#define MSG_INDEX_ID  0
#define MSG_INDEX_LEN 4
#define MSG_INDEX_SID 8
#define MSG_INDEX_AID 9
#define MSG_INDEX_FLG 10

/**
 * @brief Reads a 32-bit unsigned integer from the given source memory location.
 * We do this to avoid unaligned memory access issues on platforms that do not support it.
 */
static inline uint32_t __gracht_read_u32(const void* source) {
    uint32_t value;
    memcpy(&value, source, sizeof(value));
    return value;
}

/**
 * @brief Writes a 32-bit unsigned integer to the given destination memory location.
 * We do this to avoid unaligned memory access issues on platforms that do not support it.
 */
static inline void __gracht_write_u32(void* destination, uint32_t value) {
    memcpy(destination, &value, sizeof(value));
}

/**
 * @brief Macros to access message fields at the start of the buffer without considering the current index.
 */
#define GB_MSG_ID_0(buffer)  __gracht_read_u32(&(buffer)->data[MSG_INDEX_ID])
#define GB_MSG_LEN_0(buffer) __gracht_read_u32(&(buffer)->data[MSG_INDEX_LEN])
#define GB_MSG_SID_0(buffer) *((uint8_t*)(&((buffer)->data[MSG_INDEX_SID])))
#define GB_MSG_AID_0(buffer) *((uint8_t*)(&((buffer)->data[MSG_INDEX_AID])))
#define GB_MSG_FLG_0(buffer) *((uint8_t*)(&((buffer)->data[MSG_INDEX_FLG])))

/**
 * @brief Macros to access message fields at the current index of the buffer.
 */
#define GB_MSG_ID(buffer)  __gracht_read_u32(&(buffer)->data[(buffer)->index + MSG_INDEX_ID])
#define GB_MSG_LEN(buffer) __gracht_read_u32(&(buffer)->data[(buffer)->index + MSG_INDEX_LEN])
#define GB_MSG_SID(buffer) *((uint8_t*)(&((buffer)->data[(buffer)->index + MSG_INDEX_SID])))
#define GB_MSG_AID(buffer) *((uint8_t*)(&((buffer)->data[(buffer)->index + MSG_INDEX_AID])))
#define GB_MSG_FLG(buffer) *((uint8_t*)(&((buffer)->data[(buffer)->index + MSG_INDEX_FLG])))

/**
 * @brief Retrieves the protocol action corresponding to the given protocol ID and action ID from the provided hash table of protocols.
 * @param protocols The hash table containing the protocol definitions.
 * @param protocol_id The ID of the protocol to look up.
 * @param action_id The ID of the action within the protocol to look up.
 * @return A pointer to the corresponding protocol action, or NULL if not found.
 */
gracht_protocol_function_t* get_protocol_action(gr_hashtable_t* protocols, uint8_t protocol_id, uint8_t action_id);

/**
 * @brief Retrieves the hash value for a given protocol element.
 * @param element The protocol element for which to retrieve the hash value.
 * @return The hash of the protocol.
 */
static uint64_t protocol_hash(const void* element) {
    const struct gracht_protocol* protocol = element;
    return protocol->id;
}

/**
 * @brief Compares two protocol elements based on their IDs.
 * @param element1 The first protocol element to compare.
 * @param element2 The second protocol element to compare.
 * @return 0 if the protocol IDs are equal, 1 otherwise.
 */
static int protocol_cmp(const void* element1, const void* element2) {
    const struct gracht_protocol* protocol1 = element1;
    const struct gracht_protocol* protocol2 = element2;
    return protocol1->id == protocol2->id ? 0 : 1;
}

#endif // !__GRACHT_UTILS_H__
