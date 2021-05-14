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

#include "logging.h"
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
    struct queue   job_queue;
    cnd_t          signal;
    int            state;
};

struct gracht_worker_context {
    struct gracht_worker* worker;
    struct gracht_server* server;
};

struct gracht_worker_pool {
    struct gracht_worker* workers;
    int                   worker_count;
    int                   rr_index; // no loadbalancing atm
};

static int  worker_dowork(void*);
static void initialize_worker(struct gracht_server*, struct gracht_worker*);
static void cleanup_worker(struct gracht_worker*);

int gracht_worker_pool_create(struct gracht_server* server, int numberOfWorkers, struct gracht_worker_pool** poolOut)
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
        initialize_worker(server, &pool->workers[i]);
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
        cnd_signal(&pool->workers[i].signal);

        // wait for cleanup
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
    queue_enqueue(&worker->job_queue, recvMessage);
    mtx_unlock(&worker->sync_object);
    cnd_signal(&worker->signal);

    // increase round robin index and handle limit
    pool->rr_index++;
    if (pool->rr_index == pool->worker_count) {
        pool->rr_index = 0;
    }
}

static void initialize_worker(struct gracht_server* server, struct gracht_worker* worker)
{
    struct gracht_worker_context* context;

    context = malloc(sizeof(struct gracht_worker_context));
    if (!context) {
        GRERROR(GRSTR("initialize_worker: failed to allocate memory for worker context"));
        return;
    }

    context->worker = worker;
    context->server = server;

    queue_construct(&worker->job_queue, SERVER_WORKER_DEFAULT_QUEUE_SIZE);
    mtx_init(&worker->sync_object, mtx_plain);
    cnd_init(&worker->signal);
    worker->state = WORKER_STARTUP;

    if (thrd_create(&worker->id, worker_dowork, context) != thrd_success) {
        GRERROR(GRSTR("initialize_worker: failed to create worker-thread"));
    }
}

static void cleanup_worker(struct gracht_worker* worker)
{
    mtx_destroy(&worker->sync_object);
    cnd_destroy(&worker->signal);
    queue_destroy(&worker->job_queue);
}

static int worker_dowork(void* context)
{
    struct gracht_worker_context* workerContext = context;
    struct gracht_message*        job;
    struct gracht_worker*         worker;
    GRTRACE(GRSTR("worker_dowork: running"));

    worker = workerContext->worker;
    worker->state = WORKER_ALIVE;
    while (1) {
        mtx_lock(&worker->sync_object);
        job = queue_dequeue(&worker->job_queue);
        if (!job) {
            cnd_wait(&worker->signal, &worker->sync_object);
            if (worker->state == WORKER_SHUTDOWN_REQUEST) {
                worker->state = WORKER_SHUTDOWN;
                mtx_unlock(&worker->sync_object);
                break;
            }

            job = queue_dequeue(&worker->job_queue);
            // assert(job_header != NULL);
        }
        mtx_unlock(&worker->sync_object);

        // handle the job
        GRTRACE(GRSTR("worker_dowork: handling message"));
        server_invoke_action(workerContext->server, job);
        server_cleanup_message(workerContext->server, job);

        // check again at exit of iteration
        if (worker->state == WORKER_SHUTDOWN_REQUEST) {
            worker->state = WORKER_SHUTDOWN;
            break;
        }
    }
    GRTRACE(GRSTR("worker_dowork: shutting down"));

    job = queue_dequeue(&worker->job_queue);
    while (job) {
        server_cleanup_message(workerContext->server, job);
        job = queue_dequeue(&worker->job_queue);
    }

    free(workerContext);
    return 0;
}
