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
 * Gracht Server Dispatcher
 */

#include "logging.h"
#include "server_private.h"
#include <errno.h>
#include <os/usched/xunit.h>
#include <os/usched/job.h>
#include <stdlib.h>

struct handle_context {
    struct gracht_server* server;
    struct gracht_message* message;
};

struct gracht_worker_pool {
    struct gracht_server*  server;
};

int gracht_worker_pool_create(struct gracht_server* server, int numberOfWorkers, struct gracht_worker_pool** poolOut)
{
    struct gracht_worker_pool* pool;
    _CRT_UNUSED(numberOfWorkers);

    pool = malloc(sizeof(struct gracht_worker_pool));
    if (pool == NULL) {
        return -1;
    }

    pool->server = server;

    *poolOut = pool;
    return 0;
}

void gracht_worker_pool_destroy(struct gracht_worker_pool* pool)
{
    if (!pool) {
        return;
    }
    free(pool);
}

static void __handle_message(void* context, void* cancellationToken)
{
    struct handle_context* handleContext = context;
    _CRT_UNUSED(cancellationToken);
    GRTRACE(GRSTR("__handle_message: handling message"));
    if (handleContext == NULL) {
        GRERROR(GRSTR("__handle_message: context was NULL, ran out of memory?"));
        return;
    }

    server_invoke_action(handleContext->server, handleContext->message);
    server_cleanup_message(handleContext->server, handleContext->message);
    free(handleContext);
}

static struct handle_context* __handle_context_new(struct gracht_server* server, struct gracht_message* recvMessage)
{
    struct handle_context* context;

    context = malloc(sizeof(struct handle_context));
    if (context == NULL) {
        return NULL;
    }
    context->server = server;
    context->message = recvMessage;
    return context;
}

void gracht_worker_pool_dispatch(struct gracht_worker_pool* pool, struct gracht_message* recvMessage)
{
    if (!pool || !recvMessage) {
        return;
    }
    usched_job_queue(__handle_message, __handle_context_new(pool->server, recvMessage));
}
