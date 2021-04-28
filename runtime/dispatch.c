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
 * Gracht Server Dispatcher
 */

#include "debug.h"
#include "thread_api.h"
#include "queue.h"
#include "server_private.h"
#include <errno.h>
#include <stdlib.h>

enum gracht_worker_state {
    WORKER_STARTUP = 0,
    WORKER_ALIVE,
    WORKER_SHUTDOWN_REQUEST,
    WORKER_SHUTDOWN
};

struct gracht_worker {
    thrd_t         id;
    mtx_t          sync_object;
    gracht_queue_t job_queue;
    cnd_t          signal;
    int            state;
    void*          scratch_pad;
};

struct gracht_worker_context {
    struct gracht_worker* worker;
    struct gracht_server* server;
    int                   scratch_pad_size;
};

struct gracht_worker_pool {
    struct gracht_worker* workers;
    int                   worker_count;
    int                   rr_index; // no loadbalancing atm
};

static int  worker_dowork(void*);
static void initialize_worker(struct gracht_server*, int, struct gracht_worker*);
static void cleanup_worker(struct gracht_worker*);

__TLS_VAR struct gracht_worker* t_worker = NULL;

int gracht_worker_pool_create(struct gracht_server* server, int numberOfWorkers, int maxMessageSize, struct gracht_worker_pool** poolOut)
{
    struct gracht_worker_pool* pool;
    struct gracht_worker*      workers;
    size_t                     allocSize;
    int                        i;

    if (!poolOut || numberOfWorkers < 0) {
        errno = EINVAL;
        return -1;
    }

    allocSize = sizeof(struct gracht_worker) * numberOfWorkers;
    workers = malloc(allocSize);
    if (!workers) {
        errno = ENOMEM;
        return -1;
    }

    pool = malloc(sizeof(struct gracht_worker_pool));
    if (!pool) {
        free(workers);
        errno = ENOMEM;
        return -1;
    }

    pool->workers = workers;
    pool->worker_count = numberOfWorkers;
    pool->rr_index = 0;
    for (i = 0; i < numberOfWorkers; i++) {
        initialize_worker(server, maxMessageSize, &pool->workers[i]);
    }

    *poolOut = pool;
    return 0;
}

void gracht_worker_pool_destroy(struct gracht_worker_pool* pool)
{
    int exitCode;
    int i;

    if (!pool) {
        return;
    }

    // destroy pool of workers
    for (i = 0; i < pool->worker_count; i++) {
        pool->workers[i].state = WORKER_SHUTDOWN_REQUEST;
        thrd_join(pool->workers[i].id, &exitCode);
        cleanup_worker(&pool->workers[i]);
    }

    // cleanup resources
    free(pool->workers);
    free(pool);
}

void gracht_worker_pool_dispatch(struct gracht_worker_pool* pool, struct gracht_message* recvMessage)
{
    struct gracht_worker* worker;

    if (!pool || !recvMessage) {
        return;
    }

    worker = &pool->workers[pool->rr_index];
    mtx_lock(&worker->sync_object);
    gracht_queue_queue(&worker->job_queue, &recvMessage->header);
    mtx_unlock(&worker->sync_object);
    cnd_signal(&worker->signal);

    // increase round robin index and handle limit
    pool->rr_index++;
    if (pool->rr_index == pool->worker_count) {
        pool->rr_index = 0;
    }
}

static void initialize_worker(struct gracht_server* server, int scratchPadSize, struct gracht_worker* worker)
{
    struct gracht_worker_context* context;

    context = malloc(sizeof(struct gracht_worker_context));
    if (!context) {
        GRERROR(GRSTR("initialize_worker: failed to allocate memory for worker context"));
        return;
    }

    context->worker = worker;
    context->server = server;
    context->scratch_pad_size = scratchPadSize;

    worker->job_queue.head = NULL;
    worker->job_queue.tail = NULL;
    mtx_init(&worker->sync_object, mtx_plain);
    cnd_init(&worker->signal);
    worker->state = WORKER_STARTUP;
    worker->scratch_pad = NULL;

    if (thrd_create(&worker->id, worker_dowork, context) != thrd_success) {
        GRERROR(GRSTR("initialize_worker: failed to create worker-thread"));
    }
}

static void cleanup_worker(struct gracht_worker* worker)
{
    mtx_destroy(&worker->sync_object);
    cnd_destroy(&worker->signal);
}

void* gracht_worker_pool_get_worker_scratchpad(struct gracht_worker_pool* pool)
{
    (void)pool;
    return t_worker->scratch_pad;
}

static int worker_dowork(void* context)
{
    struct gracht_worker_context* workerContext = context;
    struct gracht_object_header*  job_header;
    struct gracht_worker*         worker;
    GRTRACE(GRSTR("worker_dowork: running"));

    worker = workerContext->worker;
    t_worker = workerContext->worker;

    // initialize the scratchpad area
    worker->scratch_pad = malloc(workerContext->scratch_pad_size);
    if (!worker->scratch_pad) {
        GRERROR(GRSTR("worker_dowork: failed to allocate memory for scratchpad"));
        return -(ENOMEM);
    }

    worker->state = WORKER_ALIVE;
    while (1) {
        mtx_lock(&worker->sync_object);
        job_header = gracht_queue_dequeue(&worker->job_queue);
        if (!job_header) {
            cnd_wait(&worker->signal, &worker->sync_object);
            if (worker->state == WORKER_SHUTDOWN_REQUEST) {
                worker->state = WORKER_SHUTDOWN;
                mtx_unlock(&worker->sync_object);
                break;
            }

            job_header = gracht_queue_dequeue(&worker->job_queue);
            // assert(job_header != NULL);
        }
        mtx_unlock(&worker->sync_object);

        // handle the job
        GRTRACE(GRSTR("worker_dowork: handling message"));
        server_invoke_action(workerContext->server, (struct gracht_message*)job_header);
        server_cleanup_message(workerContext->server, (struct gracht_message*)job_header);

        // check again at exit of iteration
        if (worker->state == WORKER_SHUTDOWN_REQUEST) {
            worker->state = WORKER_SHUTDOWN;
            break;
        }
    }
    GRTRACE(GRSTR("worker_dowork: shutting down"));

    job_header = gracht_queue_dequeue(&worker->job_queue);
    while (job_header) {
        server_cleanup_message(workerContext->server, (struct gracht_message*)job_header);
        job_header = gracht_queue_dequeue(&worker->job_queue);
    }

    free(worker->scratch_pad);
    free(workerContext);
    return 0;
}
