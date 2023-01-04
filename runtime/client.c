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
 * Gracht Client Type Definitions & Structures
 * - This header describes the base client-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <errno.h>
#include "gracht/client.h"
#include "client_private.h"
#include "arena.h"
#include "hashtable.h"
#include "logging.h"
#include "thread_api.h"
#include "control.h"
#include "utils.h"
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

// Memory requirements of the client
// On sending:
// 1 shared buffer.
// On recieving:
// N+M buffers. One for each event received and for each call in air

struct gracht_message_awaiter {
    uint32_t      id;
    unsigned int  flags;
    cnd_t         event;
    mtx_t         mutex;
    int           current_count;
    int           count;
};

struct gracht_message_awaiter_entry {
    uint32_t                       id;
    struct gracht_message_awaiter* awaiter;
};

// descriptor | message | params
struct gracht_message_descriptor {
    uint32_t        id;
    int             status;
    uint32_t        awaiter_id;
    gracht_buffer_t buffer;
};

typedef struct gracht_client {
    gracht_conn_t        iod;
    uint32_t             current_message_id;
    uint32_t             current_awaiter_id;
    struct gracht_link*  link;
    struct gracht_arena* arena;
    int                  max_message_size;
    void*                send_buffer;
    mtx_t                send_buffer_lock;
    int                  free_send_buffer;
    gr_hashtable_t       protocols;
    gr_hashtable_t       messages;
    mtx_t                messages_lock;
    gr_hashtable_t       awaiters;
    mtx_t                awaiters_lock;
    mtx_t                wait_lock;
} gracht_client_t;

#define MESSAGE_STATUS_EXECUTED(status) (status == GRACHT_MESSAGE_ERROR || status == GRACHT_MESSAGE_COMPLETED)

// api we export to generated files
GRACHTAPI int gracht_client_get_buffer(gracht_client_t*, gracht_buffer_t*);
GRACHTAPI int gracht_client_get_status_buffer(gracht_client_t*, struct gracht_message_context*, gracht_buffer_t*);
GRACHTAPI int gracht_client_status_finalize(gracht_client_t* client, struct gracht_buffer*);
GRACHTAPI int gracht_client_invoke(gracht_client_t*, struct gracht_message_context*, gracht_buffer_t*);

// static methods
static uint32_t get_message_id(gracht_client_t*);
static uint32_t get_awaiter_id(gracht_client_t*);
static void     mark_awaiters(gracht_client_t*, uint32_t);
static uint64_t message_hash(const void* element);
static int      message_cmp(const void* element1, const void* element2);
static uint64_t awaiter_hash(const void* element);
static int      awaiter_cmp(const void* element1, const void* element2);

static int __add_message(
        gracht_client_t*                   client,
        struct gracht_message_context*     context)
{
    struct gracht_message_descriptor entry = { 0 };
    if (context == NULL) {
        errno = EINVAL;
        return -1;
    }

    entry.id = context->message_id;
    entry.status = GRACHT_MESSAGE_INPROGRESS;

    mtx_lock(&client->messages_lock);
    gr_hashtable_set(&client->messages, &entry);
    mtx_unlock(&client->messages_lock);
    return 0;
}

static void __remove_message(
        gracht_client_t*                   client,
        struct gracht_message_context*     context)
{
    if (context == NULL) {
        return;
    }

    mtx_lock(&client->messages_lock);
    gr_hashtable_remove(&client->messages,
                     &(struct gracht_message_descriptor) {
        .id = context->message_id
    });
    mtx_unlock(&client->messages_lock);
}

// allocated => list_header, message_id, output_buffer
int gracht_client_invoke(
        gracht_client_t*               client,
        struct gracht_message_context* context,
        struct gracht_buffer*          message)
{
    uint32_t messageID;
    int      status;
    GRTRACE(GRSTR("gracht_client_invoke()"));
    
    if (!client || !message) {
        errno = (EINVAL);
        return -1;
    }
    
    // fill in some message details
    messageID = get_message_id(client);
    GB_MSG_ID_0(message)  = messageID;
    GB_MSG_LEN_0(message) = message->index;

    // store a copy of the message id if the context was provided.
    if (context) {
        context->message_id = messageID;
    }
    
    // require intermediate buffer for sync operations
    if (MESSAGE_FLAG_TYPE(GB_MSG_FLG_0(message)) == MESSAGE_FLAG_SYNC) {
        status = __add_message(client, context);
        if (status) {
            goto release;
        }
    }

    status = client->link->ops.client.send(client->link, message, context);
    if (status) {
        __remove_message(client, context);
    }

release:
    mtx_unlock(&client->send_buffer_lock);
    return status;
}

static int __invoke_action(gracht_client_t* client, struct gracht_buffer* message)
{
    gracht_protocol_function_t* function;
    uint8_t protocol = GB_MSG_SID(message);
    uint8_t action   = GB_MSG_AID(message);
    GRTRACE(GRSTR("__invoke_action()"));

    function = get_protocol_action(&client->protocols, protocol, action);
    if (!function) {
        return -1;
    }

    message->index += GRACHT_MESSAGE_HEADER_SIZE;
    ((client_invoke_t)function->address)(client, message);
    return 0;
}

static int __handle_response(
        gracht_client_t*      client,
        struct gracht_buffer* buffer)
{
    struct gracht_message_descriptor* descriptor;
    uint32_t                          awaiterID;
    GRTRACE(GRSTR("__handle_response()"));

    mtx_lock(&client->messages_lock);
    descriptor = gr_hashtable_get(
            &client->messages,
            &(struct gracht_message_descriptor) {
                .id = GB_MSG_ID(buffer)
            }
    );
    if (!descriptor) {
        mtx_unlock(&client->messages_lock);
        // what the heck?
        GRERROR(GRSTR("[gracht_client_wait_message] no-one was listening for message %u"), GB_MSG_ID(buffer));
        return -1;
    }

    // copy data over to message, but increase index, so it skips the meta-data
    descriptor->buffer.data  = buffer->data;
    descriptor->buffer.index = buffer->index + GRACHT_MESSAGE_HEADER_SIZE;
    descriptor->status = GRACHT_MESSAGE_COMPLETED;
    awaiterID = descriptor->awaiter_id;
    mtx_unlock(&client->messages_lock);

    // iterate awaiters and mark those that contain this message
    mark_awaiters(client, awaiterID);
    return 0;
}

int gracht_client_wait_message(
        gracht_client_t*               client,
        struct gracht_message_context* context,
        unsigned int                   flags)
{
    struct gracht_buffer buffer = { 0 };
    uint32_t             messageId = 0;
    uint8_t              messageFlags;
    int                  status;
    GRTRACE(GRSTR("gracht_client_wait_message()"));

    if (!client) {
        errno = (EINVAL);
        return -1;
    }

    // We must acquire hold of the client mutex as the mutex must be thread-safe in case
    // of multiple calling threads. Only one thread can listen for client events at a time, and that
    // means all other threads must queue up and wait for the thread that listens currently.
listenForMessage:
    // check message status
    if (context) {
        struct gracht_message_descriptor* descriptor;

        mtx_lock(&client->messages_lock);
        descriptor = gr_hashtable_get(
                &client->messages,
                &(struct gracht_message_descriptor) {
                        .id = context->message_id
                }
        );
        if (!descriptor) {
            mtx_unlock(&client->messages_lock);
            errno = ENOENT;
            return -1;
        }
        if (descriptor->status != GRACHT_MESSAGE_INPROGRESS) {
            mtx_unlock(&client->messages_lock);
            return 0;
        }
        mtx_unlock(&client->messages_lock);
    }

    if (mtx_trylock(&client->wait_lock) != thrd_success) {
        if (!(flags & GRACHT_MESSAGE_BLOCK)) {
            errno = EBUSY;
            return -1;
        }

        if (mtx_lock(&client->wait_lock) != thrd_success) {
            return -1;
        }
    }

    // initialize buffer, after this point NO returning, only jump to listenOrExit
    buffer.data = gracht_arena_allocate(client->arena, NULL, client->max_message_size);
    buffer.index = client->max_message_size;

    if (!buffer.data) {
        mtx_unlock(&client->wait_lock);
        // out of memory. client should handle messages
        errno = ENOMEM;
        status = -1;
        goto listenOrExit;
    }

    status = client->link->ops.client.recv(client->link, &buffer, flags);
    mtx_unlock(&client->wait_lock);
    if (status) {
        // In case of any recieving errors we must exit immediately
        goto listenOrExit;
    }

    messageFlags = GB_MSG_FLG(&buffer);

    // If the message is not an event, then do not invoke any actions. In any case if a context is provided
    // we expect the caller wanting to listen for that specific message. That means any incoming event and/or
    // response we recieve should be for that, or we should keep waiting.
    GRTRACE(GRSTR("[gracht] [client] message received %u - %u:%u"),
            messageFlags, GB_MSG_SID(&buffer), GB_MSG_AID(&buffer));
    if (MESSAGE_FLAG_TYPE(messageFlags) == MESSAGE_FLAG_EVENT) {
        status = __invoke_action(client, &buffer);
    } else if (MESSAGE_FLAG_TYPE(messageFlags) == MESSAGE_FLAG_RESPONSE) {
        status = __handle_response(client, &buffer);
        if (status) {
            goto listenForMessage;
        }

        // set message id handled
        messageId = GB_MSG_ID(&buffer);

        // zero the buffer pointer, so it does not get freed, freeing is now handled by
        // the awaiter
        buffer.data = NULL;
    }

listenOrExit:
    if (buffer.data) {
        gracht_arena_free(client->arena, buffer.data, 0);
    }

    if (context) {
        // In case a context was provided the meaning is that we should wait
        // for a specific message. Make sure that the message we've handled were
        // the one that was requested.
        if (messageId != 0 && context->message_id != messageId) {
            goto listenForMessage;
        }
    }
    return status;
}

static struct gracht_message_awaiter* __awaiter_new(
        gracht_client_t* client,
        unsigned int     flags,
        int              contextCount)
{
    struct gracht_message_awaiter* awaiter;

    awaiter = malloc(sizeof(struct gracht_message_awaiter));
    if (!awaiter) {
        return NULL;
    }

    awaiter->id            = get_awaiter_id(client);
    awaiter->flags         = flags;
    awaiter->count         = contextCount;
    awaiter->current_count = 0;
    cnd_init(&awaiter->event);
    mtx_init(&awaiter->mutex, mtx_plain);
    return awaiter;
}

static inline void __await_add(
        gracht_client_t*               client,
        struct gracht_message_awaiter* awaiter)
{
    mtx_lock(&client->awaiters_lock);
    gr_hashtable_set(&client->awaiters, &(struct gracht_message_awaiter_entry) {
        .id = awaiter->id,
        .awaiter = awaiter
    });
    mtx_unlock(&client->awaiters_lock);
}

static inline void __await_remove(
        gracht_client_t*               client,
        struct gracht_message_awaiter* awaiter)
{
    mtx_lock(&client->awaiters_lock);
    gr_hashtable_remove(&client->awaiters, &(struct gracht_message_awaiter_entry) {
            .id = awaiter->id
    });
    mtx_unlock(&client->awaiters_lock);
}

static inline void __await_loop(
        gracht_client_t*                client,
        struct gracht_message_context** contexts,
        struct gracht_message_awaiter*  awaiter)
{
    while (awaiter->current_count < awaiter->count) {
        gracht_client_wait_message(client, NULL, GRACHT_MESSAGE_BLOCK);

        // update status
        mtx_lock(&client->messages_lock);
        awaiter->current_count = 0;
        for (int i = 0; i < awaiter->count; i++) {
            struct gracht_message_descriptor* descriptor = gr_hashtable_get(
                    &client->messages,
                    &(struct gracht_message_descriptor) {
                            .id = contexts[i]->message_id
                    }
            );
            if (descriptor == NULL || MESSAGE_STATUS_EXECUTED(descriptor->status)) {
                awaiter->current_count++;
            }
        }
        mtx_unlock(&client->messages_lock);
    }
}

int gracht_client_await_multiple(
        gracht_client_t*                client,
        struct gracht_message_context** contexts,
        int                             contextCount,
        unsigned int                    flags)
{
    struct gracht_message_awaiter* awaiter;
    int                            i;
    bool                           bail;
    GRTRACE(GRSTR("gracht_client_await_multiple()"));
    
    if (!client || !contexts || !contextCount) {
        errno = (EINVAL);
        return -1;
    }

    // create the awaiter
    awaiter = __awaiter_new(client, flags, contextCount);
    if (awaiter == NULL) {
        return -1;
    }

    // first step is to get a status of all messages we are awaiting
    mtx_lock(&client->messages_lock);
    for (i = 0; i < contextCount; i++) {
        struct gracht_message_descriptor* descriptor = gr_hashtable_get(
                &client->messages,
                &(struct gracht_message_descriptor) {
                    .id = contexts[i]->message_id
                }
        );
        if (!descriptor) {
            // we were waiting for a non-existant message, in theory it could
            // have dissappeared?
            awaiter->current_count++;
            continue;
        }

        descriptor->awaiter_id = awaiter->id;
        if (MESSAGE_STATUS_EXECUTED(descriptor->status)) {
            awaiter->current_count++;
        }
    }

    // calculate here whether we can bail early, so we can skip time
    // not adding the awaiter
    bail = (!(flags & GRACHT_AWAIT_ALL) && awaiter->current_count > 0) || awaiter->current_count == awaiter->count;

    // add the awaiter while we hold the message lock to avoid a data-race
    // between those
    if (!bail) {
        __await_add(client, awaiter);
    }
    mtx_unlock(&client->messages_lock);

    // early bail?
    if (bail) {
        goto cleanup;
    }
    
    // in async bail mode we expect another thread to do the event pumping,
    // and thus we should just use the awaiter
    if (flags & GRACHT_AWAIT_ASYNC) {
        mtx_lock(&awaiter->mutex);
        cnd_wait(&awaiter->event, &awaiter->mutex);
        mtx_unlock(&awaiter->mutex);
    } else {
        // otherwise we are a single threaded application (maybe) and we should also
        // handle the pumping of messages.
        __await_loop(client, contexts, awaiter);
    }
    __await_remove(client, awaiter);

cleanup:
    // cleanup the awaiter
    free(awaiter);
    return 0;
}

int gracht_client_await(gracht_client_t* client, struct gracht_message_context* context, unsigned int flags)
{
    GRTRACE(GRSTR("gracht_client_await()"));
    return gracht_client_await_multiple(client, &context, 1, flags);
}

int gracht_client_get_buffer(gracht_client_t* client, gracht_buffer_t* buffer)
{
    GRTRACE(GRSTR("gracht_client_get_buffer()"));
    if (!client) {
        return -1;
    }

    mtx_lock(&client->send_buffer_lock);
    buffer->data = client->send_buffer;
    buffer->index = 0;
    return 0;
}

int gracht_client_get_status_buffer(
        gracht_client_t*               client,
        struct gracht_message_context* context,
        struct gracht_buffer*          buffer)
{
    struct gracht_message_descriptor* descriptor;
    int                               status;
    GRTRACE(GRSTR("gracht_client_get_status_buffer()"));
    
    if (!client || !context || !buffer) {
        errno = (EINVAL);
        return -1;
    }
    
    // guard against already checked
    mtx_lock(&client->messages_lock);
    descriptor = gr_hashtable_remove(
            &client->messages,
            &(struct gracht_message_descriptor) {
                    .id = context->message_id
            }
    );
    if (!descriptor) {
        mtx_unlock(&client->messages_lock);
        errno = (ENOENT);
        return -1;
    }
    
    status = descriptor->status;
    buffer->data = descriptor->buffer.data;
    buffer->index = descriptor->buffer.index;
    mtx_unlock(&client->messages_lock);

    // immediately cleanup the buffer if an error has ocurred
    if (descriptor->status == GRACHT_MESSAGE_ERROR) {
        if (descriptor->buffer.data) {
            gracht_arena_free(client->arena, descriptor->buffer.data, 0);
        }
    }
    return status;
}

int gracht_client_status_finalize(gracht_client_t* client, struct gracht_buffer* buffer)
{
    GRTRACE(GRSTR("gracht_client_status_finalize()"));
    
    if (!client) {
        errno = (EINVAL);
        return -1;
    }

    if (buffer->data) {
        gracht_arena_free(client->arena, buffer->data, 0);
    }
    return 0;
}

int gracht_client_create(gracht_client_configuration_t* config, gracht_client_t** clientOut)
{
    gracht_client_t* client;
    int              status;
    int              arenaSize;
    
    if (!config || !config->link || !clientOut) {
        GRERROR(GRSTR("[gracht] [client] config or config link was null"));
        errno = EINVAL;
        return -1;
    }
    
    client = (gracht_client_t*)malloc(sizeof(gracht_client_t));
    if (!client) {
        GRERROR(GRSTR("gracht_client: failed to allocate memory for client data"));
        errno = ENOMEM;
        return -1;
    }
    
    memset(client, 0, sizeof(gracht_client_t));
    mtx_init(&client->send_buffer_lock, mtx_plain);
    mtx_init(&client->wait_lock, mtx_plain);
    mtx_init(&client->messages_lock, mtx_plain);
    mtx_init(&client->awaiters_lock, mtx_plain);
    gr_hashtable_construct(&client->protocols, 0, sizeof(struct gracht_protocol), protocol_hash, protocol_cmp);
    gr_hashtable_construct(&client->messages, 0, sizeof(struct gracht_message_descriptor), message_hash, message_cmp);
    gr_hashtable_construct(&client->awaiters, 0, sizeof(struct gracht_message_awaiter_entry), awaiter_hash, awaiter_cmp);

    client->link = config->link;
    client->iod = GRACHT_CONN_INVALID;
    client->current_awaiter_id = 1;
    client->current_message_id = 1;

    // handle memory sizes
    client->max_message_size = config->max_message_size;
    if (client->max_message_size <= 0) {
        client->max_message_size = GRACHT_DEFAULT_MESSAGE_SIZE;
    }
    
    arenaSize = config->recv_buffer_size;
    if (arenaSize < (client->max_message_size * 2)) {
        arenaSize = client->max_message_size * 2;
    }

    // handle send buffer configuration
    client->send_buffer = config->send_buffer;
    if (!client->send_buffer) {
        client->send_buffer = malloc(client->max_message_size);
        if (!client->send_buffer) {
            GRERROR(GRSTR("gracht_client: failed to allocate memory for send buffer"));
            errno = (ENOMEM);
            goto error;
        }
        client->free_send_buffer = 1;
    }
    
    status = gracht_arena_create((size_t)arenaSize, &client->arena);
    if (status) {
        GRERROR(GRSTR("gracht_client: failed to create the memory pool"));
        errno = (ENOMEM);
        goto error;
    }

    // register the control protocol
    gracht_client_register_protocol(client, &gracht_control_client_protocol);
    *clientOut = client;
    goto exit;
    
error:
    gracht_client_shutdown(client);
    status = -1;

exit:
    return status;
}

int gracht_client_connect(gracht_client_t* client)
{
    if (!client) {
        errno = EINVAL;
        return -1;
    }

    if (client->iod != GRACHT_CONN_INVALID) {
        errno = EISCONN;
        return -1;
    }

    client->iod = client->link->ops.client.connect(client->link);
    if (client->iod == GRACHT_CONN_INVALID) {
        GRERROR(GRSTR("gracht_client: failed to connect client"));
        return -1;
    }
    return 0;
}

void gracht_client_shutdown(gracht_client_t* client)
{
    if (!client) {
        errno = (EINVAL);
        return;
    }
    
    if (client->iod != GRACHT_CONN_INVALID) {
        client->link->ops.client.destroy(client->link);
    }

    if (client->free_send_buffer) {
        free(client->send_buffer);
    }

    if (client->arena) { 
        gracht_arena_destroy(client->arena);
    }
    
    gr_hashtable_destroy(&client->awaiters);
    gr_hashtable_destroy(&client->messages);
    gr_hashtable_destroy(&client->protocols);
    mtx_destroy(&client->wait_lock);
    mtx_destroy(&client->send_buffer_lock);
    mtx_destroy(&client->messages_lock);
    mtx_destroy(&client->awaiters_lock);
    free(client);
}

gracht_conn_t gracht_client_iod(gracht_client_t* client)
{
    if (!client) {
        errno = (EINVAL);
        return -1;
    }
    return client->iod;
}

int gracht_client_register_protocol(gracht_client_t* client, gracht_protocol_t* protocol)
{
    if (!client || !protocol) {
        errno = (EINVAL);
        return -1;
    }
    
    gr_hashtable_set(&client->protocols, protocol);
    return 0;
}

void gracht_client_unregister_protocol(gracht_client_t* client, gracht_protocol_t* protocol)
{
    if (!client || !protocol) {
        errno = (EINVAL);
        return;
    }
    
    gr_hashtable_remove(&client->protocols, protocol);
}

static void mark_awaiters(gracht_client_t* client, uint32_t awaiterID)
{
    struct gracht_message_awaiter_entry* entry;
    struct gracht_message_awaiter*       awaiter;

    mtx_lock(&client->awaiters_lock);
    entry = gr_hashtable_get(
            &client->awaiters,
            &(struct gracht_message_awaiter_entry) { .id = awaiterID }
    );
    if (!entry) {
        mtx_unlock(&client->awaiters_lock);
        return;
    }
    awaiter = entry->awaiter;
    mtx_unlock(&client->awaiters_lock);

    awaiter->current_count++;
    if (awaiter->flags & GRACHT_AWAIT_ALL) {
        if (awaiter->current_count == awaiter->count) {
            cnd_signal(&awaiter->event);
        }
    } else {
        cnd_signal(&awaiter->event);
    }
}

static uint32_t get_message_id(gracht_client_t* client)
{
    return client->current_message_id++;
}

static uint32_t get_awaiter_id(gracht_client_t* client)
{
    return client->current_awaiter_id++;
}

void gracht_control_error_invocation(gracht_client_t* client, const uint32_t messageId, const int errorCode)
{
    struct gracht_message_descriptor* descriptor;
    uint32_t                          awaiterID;
    (void)errorCode;

    mtx_lock(&client->messages_lock);
    descriptor = gr_hashtable_get(
            &client->messages,
            &(struct gracht_message_descriptor) {
                .id = messageId
            }
    );
    if (!descriptor) {
        mtx_unlock(&client->messages_lock);
        // what the heck?
        GRERROR(GRSTR("gracht_control_error_invocation no-one was listening for message %u"), messageId);
        return;
    }
    
    // set status
    descriptor->status = GRACHT_MESSAGE_ERROR;
    awaiterID = descriptor->awaiter_id;
    mtx_unlock(&client->messages_lock);
    
    // iterate awaiters and mark those that contain this message
    mark_awaiters(client, awaiterID);
}

static uint64_t message_hash(const void* element)
{
    const struct gracht_message_descriptor* message = element;
    return message->id;
}

static int message_cmp(const void* element1, const void* element2)
{
    const struct gracht_message_descriptor* message1 = element1;
    const struct gracht_message_descriptor* message2 = element2;
    return message1->id == message2->id ? 0 : 1;
}

static uint64_t awaiter_hash(const void* element)
{
    const struct gracht_message_awaiter_entry* awaiter = element;
    return awaiter->id;
}

static int awaiter_cmp(const void* element1, const void* element2)
{
    const struct gracht_message_awaiter_entry* awaiter1 = element1;
    const struct gracht_message_awaiter_entry* awaiter2 = element2;
    return awaiter1->id == awaiter2->id ? 0 : 1;
}
