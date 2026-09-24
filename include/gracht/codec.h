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
 * Gracht Type Definitions & Structures
 * - This header describes the base wm-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_CODEC_H__
#define __GRACHT_CODEC_H__

#include <gracht/types.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static inline void gracht_codec_fail(gracht_buffer_t* buffer, int error)
{
    if (!buffer->error) {
        buffer->error = error;
    }
}

static inline int gracht_codec_reserve(gracht_buffer_t* buffer, size_t length)
{
    if (buffer->error) {
        return 0;
    }
    if (!buffer->data || buffer->index > buffer->limit || length > buffer->limit - buffer->index) {
        gracht_codec_fail(buffer, EMSGSIZE);
        return 0;
    }
    return 1;
}

static inline void gracht_codec_write(gracht_buffer_t* buffer, const void* source, size_t length)
{
    if (!gracht_codec_reserve(buffer, length)) {
        return;
    }
    if (length) {
        if (!source) {
            gracht_codec_fail(buffer, EINVAL);
            return;
        }
        memcpy(buffer->data + buffer->index, source, length);
    }
    buffer->index += (uint32_t)length;
}

static inline void gracht_codec_read(gracht_buffer_t* buffer, void* destination, size_t length)
{
    if (!gracht_codec_reserve(buffer, length)) {
        return;
    }
    if (length) {
        if (!destination) {
            gracht_codec_fail(buffer, EINVAL);
            return;
        }
        memcpy(destination, buffer->data + buffer->index, length);
    }
    buffer->index += (uint32_t)length;
}

static inline size_t gracht_codec_array_bytes(gracht_buffer_t* buffer, size_t element_size, uint32_t count)
{
    if (element_size && count > SIZE_MAX / element_size) {
        gracht_codec_fail(buffer, EOVERFLOW);
        return 0;
    }
    return element_size * count;
}

static inline uint32_t gracht_codec_size_add(size_t left, size_t right)
{
    if (left >= UINT32_MAX || right >= UINT32_MAX - left) {
        return UINT32_MAX;
    }
    return (uint32_t)(left + right);
}

static inline uint32_t gracht_codec_size_array(size_t element_size, uint32_t count)
{
    if (element_size && count > (UINT32_MAX - sizeof(uint32_t)) / element_size) {
        return UINT32_MAX;
    }
    return gracht_codec_size_add(sizeof(uint32_t), element_size * count);
}

static inline void* gracht_codec_allocate(gracht_buffer_t* buffer, size_t element_size,
                                        uint32_t count, size_t minimum_wire_size)
{
    size_t bytes = gracht_codec_array_bytes(buffer, element_size, count);
    void* allocation;
    if (!count || buffer->error) {
        return NULL;
    }
    if (!gracht_codec_reserve(buffer, 0)) {
        return NULL;
    }
    if (!minimum_wire_size || count > (buffer->limit - buffer->index) / minimum_wire_size || bytes > UINT32_MAX) {
        gracht_codec_fail(buffer, EMSGSIZE);
        return NULL;
    }
    allocation = calloc(1, bytes);
    if (!allocation) {
        gracht_codec_fail(buffer, ENOMEM);
    }
    return allocation;
}

#define GRACHT_SERIALIZE_VALUE(name, type) \
    static inline void serialize_##name(gracht_buffer_t* buffer, type value) { \
        gracht_codec_write(buffer, &value, sizeof(value)); \
    }

#define GRACHT_DESERIALIZE_VALUE(name, type) \
    static inline type deserialize_##name(gracht_buffer_t* buffer) { \
        type value = {0}; \
        gracht_codec_read(buffer, &value, sizeof(value)); \
        return value; \
    }

#endif