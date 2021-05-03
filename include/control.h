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
 * Gracht Control Protocol Type Definitions & Structures
 * - This header describes the base control-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_CONTROL_PROTOCOL_H__
#define __GRACHT_CONTROL_PROTOCOL_H__

#include "gracht/types.h"
#include "gracht/server.h"
#include "gracht/client.h"

#define SERVICE_GRACHT_CONTROL_ID 0
#define SERVICE_GRACHT_CONTROL_FUNCTION_COUNT 2

#define SERVICE_GRACHT_CONTROL_SUBSCRIBE_ID 0
#define SERVICE_GRACHT_CONTROL_UNSUBSCRIBE_ID 1

#define SERVICE_GRACHT_CONTROL_EVENT_ERROR_ID 2

// Server part of the internal control protocol
struct gracht_transfer_complete_event {
    uint32_t id;
};

void gracht_control_subscribe_invocation(const struct gracht_message* message, const uint8_t protocol);
void gracht_control_unsubscribe_invocation(const struct gracht_message* message, const uint8_t protocol);

int gracht_control_event_error_single(gracht_server_t* server, const gracht_conn_t client, const uint32_t messageId, const int errorCode);
int gracht_control_event_error_all(gracht_server_t* server, const uint32_t messageId, const int errorCode);

// Client part of the internal control protocol
void gracht_control_error_invocation(gracht_client_t* client, const uint32_t messageId, const int errorCode);

extern gracht_protocol_t gracht_control_server_protocol;
extern gracht_protocol_t gracht_control_client_protocol;

#endif // !__GRACHT_CONTROL_PROTOCOL_H__
