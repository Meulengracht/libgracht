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
 * Gracht Server Configuration Helpers
 */

#include "include/gracht/client.h"
#include <string.h>

void gracht_client_configuration_init(gracht_client_configuration_t* config)
{
    memset(config, 0, sizeof(gracht_client_configuration_t));
    config->max_message_size = GRACHT_DEFAULT_MESSAGE_SIZE;
}

void gracht_client_configuration_set_link(gracht_client_configuration_t* config, struct client_link_ops* link)
{
    config->link = link;
}

void gracht_client_configuration_set_msg_buffer(gracht_client_configuration_t* config, void* buffer)
{
    config->message_buffer = buffer;
}

void gracht_client_configuration_set_max_msg_size(gracht_client_configuration_t* config, int maxMessageSize)
{
    config->max_message_size = maxMessageSize;
}
