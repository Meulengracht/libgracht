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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Gracht Client Type Definitions & Structures
 * - This header describes the base client-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <assert.h>
#include <errno.h>
#include "gracht/client.h"
#include "client_private.h"
#include "arena.h"
#include "crc.h"
#include "hashtable.h"
#include "debug.h"
#include "thread_api.h"
#include "control.h"
#include "utils.h"
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
    int           current_count;
    int           count;
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
    int                  free_send_buffer;
    hashtable_t          protocols;
    hashtable_t          messages;
    hashtable_t          awaiters;
    mtx_t                data_lock;
    mtx_t                wait_lock;
} gracht_client_t;

#define MESSAGE_STATUS_EXECUTED(status) (status == GRACHT_MESSAGE_ERROR || status == GRACHT_MESSAGE_COMPLETED)

// api we export to generated files
GRACHTAPI int gracht_client_get_buffer(gracht_client_t*, gracht_buffer_t*);
GRACHTAPI int gracht_client_get_status_buffer(gracht_client_t*, struct gracht_message_context*, gracht_buffer_t*);
GRACHTAPI int gracht_client_status_finalize(gracht_client_t*, struct gracht_message_context*);
GRACHTAPI int gracht_client_invoke(gracht_client_t*, struct gracht_message_context*, gracht_buffer_t*);

// static methods
static uint32_t get_message_id(gracht_client_t*);
static uint32_t get_awaiter_id(gracht_client_t*);
static void     mark_awaiters(gracht_client_t*, struct gracht_message_descriptor*);
static uint64_t message_hash(const void* element);
static int      message_cmp(const void* element1, const void* element2);
static uint64_t awaiter_hash(const void* element);
static int      awaiter_cmp(const void* element1, const void* element2);

// allocated => list_header, message_id, output_buffer
int gracht_client_invoke(gracht_client_t* client, struct gracht_message_context* context, struct gracht_buffer* message)
{
    struct gracht_message_descriptor* descriptor = NULL;
    int status;
    
    if (!client || !message) {
        errno = (EINVAL);
        return -1;
    }
    
    // fill in some message details
    GB_MSG_ID_0(message)  = get_message_id(client);
    GB_MSG_LEN_0(message) = message->index;
    
    // require intermediate buffer for sync operations
    if (MESSAGE_FLAG_TYPE(GB_MSG_FLG_0(message)) == MESSAGE_FLAG_SYNC) {
        struct gracht_message_descriptor entry = { 0 };
        if (!context) {
            errno = EINVAL;
            return -1;
        }

        context->message_id = GB_MSG_ID_0(message);

        entry.id = GB_MSG_ID_0(message);
        entry.status = GRACHT_MESSAGE_CREATED;
        
        mtx_lock(&client->data_lock);
        hashtable_set(&client->messages, &entry);
        descriptor = hashtable_get(&client->messages, &entry);
        mtx_unlock(&client->data_lock);
    }
    
    status = client->link->ops.client.send(client->link, message, context);
    if (descriptor) {
        descriptor->status = status;
    }
    return status == GRACHT_MESSAGE_ERROR ? -1 : 0;
}

int gracht_client_await_multiple(gracht_client_t* client,
    struct gracht_message_context** contexts, int contextCount, unsigned int flags)
{
    struct gracht_message_awaiter awaiter;
    int                           i;
    
    if (!client || !contexts || !contextCount) {
        errno = (EINVAL);
        return -1;
    }
    
    // delay init of condition
    awaiter.id            = get_awaiter_id(client);
    awaiter.flags         = flags;
    awaiter.count         = contextCount;
    awaiter.current_count = 0;

    // register the awaiter
    mtx_lock(&client->data_lock);
    for (i = 0; i < contextCount; i++) {
        struct gracht_message_descriptor* descriptor = hashtable_get(&client->messages,
            &(struct gracht_message_descriptor) { .id = contexts[i]->message_id });
        if (descriptor) {
            descriptor->awaiter_id = awaiter.id;
            if (MESSAGE_STATUS_EXECUTED(descriptor->status)) {
                awaiter.current_count++;
            }
        }
    }

    if ((!(flags & GRACHT_AWAIT_ALL) && awaiter.current_count > 0) ||
        awaiter.current_count == awaiter.count) {
        // all requested messages were already executed
        mtx_unlock(&client->data_lock);
        return 0;
    }
    
    // in async wait mode we expect another thread to do the event pumping
    // and thus we should just use the awaiter
    if (flags & GRACHT_AWAIT_ASYNC) {
        struct gracht_message_awaiter* sharedAwaiter;
        cnd_init(&awaiter.event);
        hashtable_set(&client->awaiters, &awaiter);
        sharedAwaiter = hashtable_get(&client->awaiters, &awaiter);
        cnd_wait(&sharedAwaiter->event, &client->data_lock);
        sharedAwaiter = hashtable_remove(&client->awaiters, &awaiter);
        cnd_destroy(&sharedAwaiter->event);
        mtx_unlock(&client->data_lock);
    }
    else {
        mtx_unlock(&client->data_lock);

        // otherwise we a single threaded application (maybe) and we should also
        // handle the pumping of messages.
        while (awaiter.current_count < awaiter.count) {
            gracht_client_wait_message(client, NULL, GRACHT_MESSAGE_BLOCK);

            // update status
            mtx_lock(&client->data_lock);
            awaiter.current_count = 0;
            for (i = 0; i < contextCount; i++) {
                struct gracht_message_descriptor* descriptor = hashtable_get(&client->messages,
                    &(struct gracht_message_descriptor) { .id = contexts[i]->message_id });
                if (descriptor) {
                    if (MESSAGE_STATUS_EXECUTED(descriptor->status)) {
                        awaiter.current_count++;
                    }
                }
            }
            mtx_unlock(&client->data_lock);
        }
    }
    return 0;
}

int gracht_client_await(gracht_client_t* client, struct gracht_message_context* context, unsigned int flags)
{
    return gracht_client_await_multiple(client, &context, 1, flags);
}

int gracht_client_get_buffer(gracht_client_t* client, gracht_buffer_t* buffer)
{
    if (!client) {
        return -1;
    }

    // @todo this buffer should be locked.
    buffer->data = client->send_buffer;
    buffer->index = 0;
    return 0;
}

int gracht_client_get_status_buffer(gracht_client_t* client, struct gracht_message_context* context,
    struct gracht_buffer* buffer)
{
    struct gracht_message_descriptor* descriptor;
    int                               status;
    GRTRACE(GRSTR("[gracht] [client] get status from context"));
    
    if (!client || !context || !buffer) {
        errno = (EINVAL);
        return -1;
    }
    
    // guard against already checked
    mtx_lock(&client->data_lock);
    descriptor = (struct gracht_message_descriptor*)hashtable_get(&client->messages, 
        &(struct gracht_message_descriptor) { .id = context->message_id });
    if (!descriptor) {
        mtx_unlock(&client->data_lock);

        GRERROR(GRSTR("[gracht] [client] descriptor for message was not found"));
        errno = (EALREADY);
        return -1;
    }
    
    status = descriptor->status;
    if (descriptor->status == GRACHT_MESSAGE_ERROR) {
        if (descriptor->buffer.data) {
            gracht_arena_free(client->arena, descriptor->buffer.data, 0);
        }
        hashtable_remove(&client->messages, &(struct gracht_message_descriptor) { .id = context->message_id });
    }
    mtx_unlock(&client->data_lock);

    if (status == GRACHT_MESSAGE_COMPLETED) {
        buffer->data  = descriptor->buffer.data;
        buffer->index = descriptor->buffer.index;
    }
    return status;
}

int gracht_client_status_finalize(gracht_client_t* client, struct gracht_message_context* context)
{
    struct gracht_message_descriptor* descriptor;
    GRTRACE(GRSTR("gracht_client_status_finalize()"));
    
    if (!client || !context) {
        errno = (EINVAL);
        return -1;
    }
    
    // guard against already checked
    mtx_lock(&client->data_lock);
    descriptor = (struct gracht_message_descriptor*)hashtable_remove(&client->messages, 
        &(struct gracht_message_descriptor) { .id = context->message_id });
    if (descriptor && descriptor->buffer.data) {
        gracht_arena_free(client->arena, descriptor->buffer.data, 0);
    }
    mtx_unlock(&client->data_lock);
    if (!descriptor) {
        GRERROR(GRSTR("gracht_client_status_finalize descriptor for message was not found"));
        errno = (EALREADY);
        return -1;
    }
    return 0;
}

int client_invoke_action(gracht_client_t* client, struct gracht_buffer* message)
{
    gracht_protocol_function_t* function;
    uint8_t protocol = GB_MSG_SID(message);
    uint8_t action   = GB_MSG_AID(message);

    function = get_protocol_action(&client->protocols, protocol, action);
    if (!function) {
        return -1;
    }

    message->index += GRACHT_MESSAGE_HEADER_SIZE;
    ((client_invoke_t)function->address)(client, message);
    return 0;
}

int gracht_client_wait_message(
        gracht_client_t*               client,
        struct gracht_message_context* context,
        unsigned int                   flags)
{
    struct gracht_buffer buffer;
    uint32_t             messageId = 0;
    uint8_t              messageFlags;
    int                  status;
    
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
        struct gracht_message_descriptor* descriptor = hashtable_get(&client->messages, 
            &(struct gracht_message_descriptor) { .id = context->message_id });
        if (!descriptor) {
            return -1;
        }
        if (descriptor->status != GRACHT_MESSAGE_INPROGRESS) {
            return 0;
        }
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

    // initialize buffer
    mtx_lock(&client->data_lock);
    buffer.data = gracht_arena_allocate(client->arena, NULL, client->max_message_size);
    mtx_unlock(&client->data_lock);
    buffer.index = client->max_message_size;

    if (!buffer.data) {
        mtx_unlock(&client->wait_lock);
        // out of memory. client should handle messages
        errno = ENOMEM;
        return -1;
    }

    status = client->link->ops.client.recv(client->link, &buffer, flags);
    mtx_unlock(&client->wait_lock);
    if (status) {
        // In case of any recieving errors we must exit immediately
        return status;
    }
    
    messageFlags = GB_MSG_FLG(&buffer);
    
    // If the message is not an event, then do not invoke any actions. In any case if a context is provided
    // we expect the caller to wanting to listen for that specific message. That means any incoming event and/or
    // response we recieve should be for that, or we should keep waiting.
    GRTRACE(GRSTR("[gracht] [client] message received %u - %u:%u"),
        messageFlags, GB_MSG_SID(&buffer), GB_MSG_AID(&buffer));
    if (MESSAGE_FLAG_TYPE(messageFlags) == MESSAGE_FLAG_EVENT) {
        status = client_invoke_action(client, &buffer);
        
        // cleanup buffer after handling event
        mtx_lock(&client->data_lock);
        gracht_arena_free(client->arena, buffer.data, 0);
        mtx_unlock(&client->data_lock);
    }
    else if (MESSAGE_FLAG_TYPE(messageFlags) == MESSAGE_FLAG_RESPONSE) {
        struct gracht_message_descriptor* descriptor = hashtable_get(&client->messages, 
            &(struct gracht_message_descriptor) { .id = GB_MSG_ID(&buffer) });
        if (!descriptor) {
            // what the heck?
            GRERROR(GRSTR("[gracht_client_wait_message] no-one was listening for message %u"), GB_MSG_ID(&buffer));
            status = -1;
            goto listenOrExit;
        }
        
        // set message id handled
        messageId = GB_MSG_ID(&buffer);
        
        // copy data over to message, but increase index so it skips the meta-data
        descriptor->buffer.data  = buffer.data;
        descriptor->buffer.index = buffer.index + GRACHT_MESSAGE_HEADER_SIZE;
        descriptor->status = GRACHT_MESSAGE_COMPLETED;

        // iterate awaiters and mark those that contain this message
        mtx_lock(&client->data_lock);
        mark_awaiters(client, descriptor);
        mtx_unlock(&client->data_lock);
    }

listenOrExit:
    if (context) {
        // In case a context was provided the meaning is that we should wait
        // for a specific message. Make sure that the message we've handled were
        // the one that was requested.
        if (context->message_id != messageId) {
            goto listenForMessage;
        }
    }
    return status;
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
    mtx_init(&client->data_lock, mtx_plain);
    mtx_init(&client->wait_lock, mtx_plain);
    hashtable_construct(&client->protocols, 0, sizeof(struct gracht_protocol), protocol_hash, protocol_cmp);
    hashtable_construct(&client->messages, 0, sizeof(struct gracht_message_descriptor), message_hash, message_cmp);
    hashtable_construct(&client->awaiters, 0, sizeof(struct gracht_message_awaiter), awaiter_hash, awaiter_cmp);

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
    
    hashtable_destroy(&client->awaiters);
    hashtable_destroy(&client->messages);
    hashtable_destroy(&client->protocols);
    mtx_destroy(&client->data_lock);
    mtx_destroy(&client->wait_lock);
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
    
    hashtable_set(&client->protocols, protocol);
    return 0;
}

void gracht_client_unregister_protocol(gracht_client_t* client, gracht_protocol_t* protocol)
{
    if (!client || !protocol) {
        errno = (EINVAL);
        return;
    }
    
    hashtable_remove(&client->protocols, protocol);
}

static void mark_awaiters(gracht_client_t* client, struct gracht_message_descriptor* descriptor)
{
    struct gracht_message_awaiter* awaiter = hashtable_get(&client->awaiters, &(struct gracht_message_awaiter) { .id = descriptor->awaiter_id });
    if (!awaiter) {
        return;
    }

    awaiter->current_count++;
    if (awaiter->flags & GRACHT_AWAIT_ALL) {
        if (awaiter->current_count == awaiter->count) {
            cnd_signal(&awaiter->event);
        }
    }
    else {
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
    struct gracht_message_descriptor* descriptor = 
        hashtable_get(&client->messages, &(struct gracht_message_descriptor) { .id = messageId });
    (void)errorCode;

    if (!descriptor) {
        // what the heck?
        GRERROR(GRSTR("gracht_control_error_invocation no-one was listening for message %u"), messageId);
        return;
    }
    
    // set status
    descriptor->status = GRACHT_MESSAGE_ERROR;
    
    // iterate awaiters and mark those that contain this message
    mtx_lock(&client->data_lock);
    mark_awaiters(client, descriptor);
    mtx_unlock(&client->data_lock);
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
    const struct gracht_message_awaiter* awaiter = element;
    return awaiter->id;
}

static int awaiter_cmp(const void* element1, const void* element2)
{
    const struct gracht_message_awaiter* awaiter1 = element1;
    const struct gracht_message_awaiter* awaiter2 = element2;
    return awaiter1->id == awaiter2->id ? 0 : 1;
}
