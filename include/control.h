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
 * Gracht Control Protocol Type Definitions & Structures
 * - This header describes the base control-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_CONTROL_PROTOCOL_H__
#define __GRACHT_CONTROL_PROTOCOL_H__

#include "gracht/types.h"
#include "gracht/server.h"
#include "gracht/client.h"
#include "gracht/capability.h"

#define SERVICE_GRACHT_CONTROL_ID 0
#define SERVICE_GRACHT_CONTROL_FUNCTION_COUNT 3

#define SERVICE_GRACHT_CONTROL_SUBSCRIBE_ID   0
#define SERVICE_GRACHT_CONTROL_UNSUBSCRIBE_ID 1
#define SERVICE_GRACHT_CONTROL_NEGOTIATE_ID   2

#define SERVICE_GRACHT_CONTROL_EVENT_ERROR_ID              2
#define SERVICE_GRACHT_CONTROL_EVENT_NEGOTIATE_RESPONSE_ID 3

// Server part of the internal control protocol
struct gracht_transfer_complete_event {
    uint32_t id;
};

void gracht_control_subscribe_invocation(const struct gracht_message* message, const uint8_t protocol);
void gracht_control_unsubscribe_invocation(const struct gracht_message* message, const uint8_t protocol);
void gracht_control_negotiate_invocation(const struct gracht_message* message, const struct gracht_capabilities* clientCaps);

int gracht_control_event_error_single(gracht_server_t* server, const gracht_conn_t client, const uint32_t messageId, const int errorCode);
int gracht_control_event_error_all(gracht_server_t* server, const uint32_t messageId, const int errorCode);
int gracht_control_negotiate_response_event(gracht_server_t* server, gracht_conn_t client, const struct gracht_capabilities* caps);

// Client part of the internal control protocol
void gracht_control_error_invocation(gracht_client_t* client, const uint32_t messageId, const int errorCode);

/**
 * Sends the local capabilities to the server as part of connection negotiation.
 * Called internally by gracht_client_connect(). The client must wait for the
 * corresponding negotiate_response event before the effective capabilities are
 * available via gracht_client_get_capabilities().
 *
 * @param client A pointer to the client instance.
 * @return int Returns 0 on success, or -1 on error.
 */
int gracht_control_negotiate(gracht_client_t* client);

/**
 * Internal helper called by the control protocol handler once the
 * negotiate_response event has been received and decoded. Stores the
 * effective capabilities into the client and signals that negotiation
 * is complete.
 *
 * @param client  A pointer to the client instance.
 * @param caps    A pointer to the effective capabilities from the server.
 */
void gracht_client_store_capabilities(gracht_client_t* client, const struct gracht_capabilities* caps);

extern gracht_protocol_t gracht_control_server_protocol;
extern gracht_protocol_t gracht_control_client_protocol;

#endif // !__GRACHT_CONTROL_PROTOCOL_H__
