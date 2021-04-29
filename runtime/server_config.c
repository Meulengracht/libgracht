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

#include "gracht/server.h"
#include <string.h>

void gracht_server_configuration_init(gracht_server_configuration_t* config)
{
    memset(config, 0, sizeof(gracht_server_configuration_t));
    config->server_workers = 1;
    config->max_message_size = GRACHT_DEFAULT_MESSAGE_SIZE;
}

void gracht_server_configuration_set_aio_descriptor(gracht_server_configuration_t* config, gracht_handle_t descriptor)
{
    config->set_descriptor = descriptor;
    config->set_descriptor_provided = 1;
}

void gracht_server_configuration_set_num_workers(gracht_server_configuration_t* config, int workerCount)
{
    config->server_workers = workerCount;
}

void gracht_server_configuration_set_max_msg_size(gracht_server_configuration_t* config, int maxMessageSize)
{
    config->max_message_size = maxMessageSize;
}
