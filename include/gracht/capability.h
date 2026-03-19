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
 * Gracht Capability Negotiation Type Definitions & Structures
 * - This header describes the capability structures used during the
 *   initial negotiation phase between client and server.
 */

#ifndef __GRACHT_CAPABILITY_H__
#define __GRACHT_CAPABILITY_H__

#include <stdint.h>

/**
 * Current version of the gracht control protocol.
 * Both peers must advertise this version during negotiation.
 */
#define GRACHT_PROTOCOL_VERSION 1

/**
 * Bitmask values for the supported_checksums capability field.
 * GRACHT_CHECKSUM_NONE indicates that no checksum algorithm is supported.
 * GRACHT_CHECKSUM_CRC32 indicates that CRC-32 is supported.
 */
#define GRACHT_CHECKSUM_NONE  0x00000000
#define GRACHT_CHECKSUM_CRC32 0x00000001

/**
 * Describes the full set of capabilities negotiated between a client and
 * server. After a successful call to gracht_client_connect() the client
 * holds the effective (intersected) capabilities agreed upon by both peers.
 * The server similarly stores the effective capabilities per connected client.
 *
 * Fields that are boolean (stream_support, reliable_stream_support,
 * live_stream_support) are represented as uint8_t where 0 = false, 1 = true.
 * For boolean fields the negotiated value is the logical AND of both peers.
 * For numeric bounds the negotiated value is the minimum of both peers.
 * For bitmask fields (supported_checksums) the negotiated value is the
 * bitwise AND of both peers.
 */
struct gracht_capabilities {
    /** Version of the gracht control protocol spoken by this peer. */
    uint32_t protocol_version;

    /** Maximum size in bytes of a single normal (non-stream) message. */
    uint32_t max_message_size;

    /** Whether this peer supports streams at all. */
    uint8_t  stream_support;

    /** Whether reliable (lossless, ordered) streams are supported. */
    uint8_t  reliable_stream_support;

    /** Whether live (best-effort) streams are supported. */
    uint8_t  live_stream_support;

    /** Maximum number of concurrently open streams. */
    uint32_t max_concurrent_streams;

    /** Maximum size in bytes of a single reliable-stream chunk. */
    uint32_t max_reliable_chunk_size;

    /** Maximum size in bytes of a single live-stream unit. */
    uint32_t max_live_unit_size;

    /** Default size in bytes of the reliable-stream receive window. */
    uint32_t default_recv_window;

    /** Bitmask of checksum algorithms supported (see GRACHT_CHECKSUM_*). */
    uint32_t supported_checksums;

    /** Maximum size in bytes of stream metadata. */
    uint32_t max_stream_metadata_size;

    /** Maximum size in bytes of a stream resume token. */
    uint32_t max_resume_token_size;
};

#endif // !__GRACHT_CAPABILITY_H__
