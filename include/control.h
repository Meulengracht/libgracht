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

#define GRACHT_CONTROL_PROTOCOL_SUBSCRIBE_ID   0
#define GRACHT_CONTROL_PROTOCOL_UNSUBSCRIBE_ID 1
#define GRACHT_CONTROL_PROTOCOL_ERROR_EVENT_ID 2

// Server part of the internal control protocol
GRACHT_STRUCT(gracht_subscription_args, {
    uint8_t protocol_id;
});

GRACHT_STRUCT(gracht_control_error_event, {
    uint32_t message_id;
    int      error_code;
});

void gracht_control_subscribe_callback(struct gracht_recv_message* message, struct gracht_subscription_args*);
void gracht_control_unsubscribe_callback(struct gracht_recv_message* message, struct gracht_subscription_args*);

static int gracht_control_event_error_single(gracht_conn_t client, uint32_t message_id, int error_code)
{
    struct {
        struct gracht_message_header __base;
        struct gracht_param          __params[2];
    } __message = { .__base = { 
        .id = 0,
        .length = sizeof(struct gracht_message) + (2 * sizeof(struct gracht_param)),
        .param_in = 2,
        .param_out = 0,
        .flags = MESSAGE_FLAG_EVENT,
        .protocol = 0,
        .action = 1
    }, .__params = {
            { .type = GRACHT_PARAM_VALUE, .data.value = (size_t)message_id, .length = sizeof(uint32_t) },
            { .type = GRACHT_PARAM_VALUE, .data.value = (size_t)error_code, .length = sizeof(int) }
        }
    };

    return gracht_server_send_event(client, (struct gracht_message*)&__message, 0);
}

// Client part of the internal control protocol
void gracht_control_error_event(gracht_client_t* client, struct gracht_control_error_event* event);

#endif // !__GRACHT_CONTROL_PROTOCOL_H__
