/**
 * MollenOS
 *
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
#include "include/gracht/client.h"
#include "include/client_private.h"
#include "include/crc.h"
#include "include/hashtable.h"
#include "include/debug.h"
#include "include/thread_api.h"
#include "include/control.h"
#include "include/utils.h"
#include <string.h>
#include <stdlib.h>

struct gracht_message_awaiter {
    uint32_t      id;
    unsigned int  flags;
    cnd_t         event;
    int           current_count;
    int           count;
};

// descriptor | message | params
struct gracht_message_descriptor {
    int                   status;
    uint32_t              awaiter_id;
    struct gracht_message message;
};

typedef struct gracht_client {
    gracht_conn_t           iod;
    uint32_t                current_message_id;
    uint32_t                current_awaiter_id;
    struct client_link_ops* ops;
    int                     max_message_size;
    void*                   message_buffer;
    int                     free_buffer;
    hashtable_t             protocols;
    hashtable_t             messages;
    hashtable_t             awaiters;
    mtx_t                   data_lock;
    mtx_t                   wait_lock;
} gracht_client_t;

#define MESSAGE_STATUS_EXECUTED(status) (status == GRACHT_MESSAGE_ERROR || status == GRACHT_MESSAGE_COMPLETED)

// static methods
static uint32_t get_message_id(gracht_client_t*);
static uint32_t get_awaiter_id(gracht_client_t*);
static void     mark_awaiters(gracht_client_t*, struct gracht_message_descriptor*);
static uint64_t message_hash(const void* element);
static int      message_cmp(const void* element1, const void* element2);
static uint64_t awaiter_hash(const void* element);
static int      awaiter_cmp(const void* element1, const void* element2);

static gracht_protocol_function_t control_events[1] = {
   { GRACHT_CONTROL_PROTOCOL_ERROR_EVENT_ID , gracht_control_error_event }
};
static gracht_protocol_t control_protocol = GRACHT_PROTOCOL_INIT(0, "gctrl", 1, control_events);

// allocated => list_header, message_id, output_buffer
int gracht_client_invoke(gracht_client_t* client, struct gracht_message_context* context,
    struct gracht_message* message)
{
    struct gracht_message_descriptor* descriptor = NULL;
    int status;
    
    if (!client || !message) {
        errno = (EINVAL);
        return -1;
    }
    
    // fill in some message details
    message->header.id = get_message_id(client);
    
    // require intermediate buffer for sync operations
    if (MESSAGE_FLAG_TYPE(message->header.flags) == MESSAGE_FLAG_SYNC) {
        size_t   bufferLength = sizeof(struct gracht_message_descriptor) + (message->header.param_out * sizeof(struct gracht_param));
        uint32_t i;

        if (!context) {
            errno = EINVAL;
            return -1;
        }

        for (i = 0; i < message->header.param_out; i++) {
            if (message->params[message->header.param_in + i].type == GRACHT_PARAM_BUFFER) {
                bufferLength += message->params[message->header.param_in + i].length;
            }
        }
        
        context->message_id      = message->header.id;
        context->internal_handle = malloc(bufferLength);
        if (!context->internal_handle) {
            errno = ENOMEM;
            return -1;
        }
        
        descriptor = context->internal_handle;
        descriptor->status = GRACHT_MESSAGE_CREATED;
        descriptor->awaiter_id = 0;
        
        mtx_lock(&client->data_lock);
        hashtable_set(&client->messages, context);
        mtx_unlock(&client->data_lock);
    }
    
    status = client->ops->send(client->ops, message, context);
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
        struct gracht_message_context*    message = hashtable_get(&client->messages,
            &(struct gracht_message_context) { .message_id = contexts[i]->message_id });
        struct gracht_message_descriptor* descriptor = message ? message->internal_handle : NULL;
        if (descriptor) {
            descriptor->awaiter_id = awaiter.id;
            if (MESSAGE_STATUS_EXECUTED(descriptor->status)) {
                awaiter.current_count++;
            }
        }
    }

    if ((!(flags &GRACHT_AWAIT_ALL) && awaiter.current_count > 0) ||
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
                struct gracht_message_context*    message = hashtable_get(&client->messages,
                    &(struct gracht_message_context) { .message_id = contexts[i]->message_id });
                struct gracht_message_descriptor* descriptor = message ? message->internal_handle : NULL;
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

// status, output_buffer
int gracht_client_status(gracht_client_t* client, struct gracht_message_context* context,
    struct gracht_param* params)
{
    struct gracht_message_descriptor* descriptor;
    char*                             pointer = NULL;
    int                               status;
    uint32_t                          i;
    GRTRACE(GRSTR("[gracht] [client] get status from context"));
    
    if (!client || !context || !params) {
        errno = (EINVAL);
        return -1;
    }
    
    // guard against already checked
    mtx_lock(&client->data_lock);
    descriptor = (struct gracht_message_descriptor*)context->internal_handle;
    if (!descriptor) {
        mtx_unlock(&client->data_lock);

        GRERROR(GRSTR("[gracht] [client] descriptor for message was not found"));
        errno = (EALREADY);
        return -1;
    }
    
    if (descriptor->status == GRACHT_MESSAGE_COMPLETED || descriptor->status == GRACHT_MESSAGE_ERROR) {
        hashtable_remove(&client->messages, &(struct gracht_message_context) { .message_id = context->message_id });
    }
    mtx_unlock(&client->data_lock);

    if (descriptor->status == GRACHT_MESSAGE_COMPLETED) {
        pointer = (char*)&descriptor->message.params[descriptor->message.header.param_in];

        GRTRACE(GRSTR("[gracht] [client] unpacking parameters"));
        for (i = 0; i < descriptor->message.header.param_in; i++) {
            struct gracht_param* out_param = &params[i];
            struct gracht_param* in_param  = &descriptor->message.params[i];
            
            if (out_param->data.buffer && out_param->type == GRACHT_PARAM_VALUE) {
                if (out_param->length == 1) {
                    *((uint8_t*)out_param->data.buffer) = (uint8_t)(in_param->data.value & 0xFF);
                }
                else if (out_param->length == 2) {
                    *((uint16_t*)out_param->data.buffer) = (uint16_t)(in_param->data.value & 0xFFFF);
                }
                else if (out_param->length == 4) {
                    *((uint32_t*)out_param->data.buffer) = (uint32_t)(in_param->data.value & 0xFFFFFFFF);
                }
                else if (out_param->length == 8) {
                    *((uint64_t*)out_param->data.buffer) = (uint64_t)in_param->data.value;
                }
            }
            else if (out_param->type == GRACHT_PARAM_BUFFER) {
                if (out_param->data.buffer) {
                    memcpy(out_param->data.buffer, pointer, in_param->length);
                }
                pointer += in_param->length;
            }
        }
        
    }

    status = descriptor->status;
    if (descriptor->status == GRACHT_MESSAGE_COMPLETED || descriptor->status == GRACHT_MESSAGE_ERROR) {
        free(descriptor);
    }
    return status;
}

int client_invoke_action(gracht_client_t* client, struct gracht_message* message)
{
    gracht_protocol_function_t* function = get_protocol_action(&client->protocols,
        message->header.protocol, message->header.action);
    uint32_t param_count;
    void*    param_storage;

    if (!function) {
        return -1;
    }
    
    param_count   = message->header.param_in + message->header.param_out;
    param_storage = (char*)message + sizeof(struct gracht_message) +
            (message->header.param_in * sizeof(struct gracht_param));
    
    // parse parameters into a parameter struct
    GRTRACE(GRSTR("offset: %lu, param count %i"), param_count * sizeof(struct gracht_param), param_count);
    if (param_count) {
        uint8_t* unpackBuffer = alloca(param_count * sizeof(void*));
        unpack_parameters(&message->params[0], message->header.param_in, param_storage, &unpackBuffer[0]);
        ((client_invokeA0_t)function->address)(client, &unpackBuffer[0]);
    }
    else {
        ((client_invoke00_t)function->address)(client);
    }
    return 0;
}

int gracht_client_wait_message(
        gracht_client_t*               client,
        struct gracht_message_context* context,
        unsigned int                   flags)
{
    struct gracht_message* message;
    int                    status;
    uint32_t               messageId = 0;
    
    if (!client) {
        errno = (EINVAL);
        return -1;
    }

    // We must acquire hold of the client mutex as the mutex must be thread-safe in case
    // of multiple calling threads. Only one thread can listen for client events at a time, and that
    // means all other threads must queue up and wait for the thread that listens currently.
listenForMessage:
    if (mtx_trylock(&client->wait_lock) != thrd_success) {
        if (!(flags & GRACHT_MESSAGE_BLOCK)) {
            errno = EBUSY;
            return -1;
        }

        if (mtx_lock(&client->wait_lock) != thrd_success) {
            return -1;
        }
    }

    status = client->ops->recv(client->ops, client->message_buffer, flags, &message);
    mtx_unlock(&client->wait_lock);
    if (status) {
        // In case of any recieving errors we must exit immediately
        return status;
    }
    
    // If the message is not an event, then do not invoke any actions. In any case if a context is provided
    // we expect the caller to wanting to listen for that specific message. That means any incoming event and/or
    // response we recieve should be for that, or we should keep waiting.
    GRTRACE(GRSTR("[gracht] [client] invoking message type %u - %u/%u"),
        message->header.flags, message->header.protocol, message->header.action);
    if (MESSAGE_FLAG_TYPE(message->header.flags) == MESSAGE_FLAG_EVENT) {
        status = client_invoke_action(client, message);
    }
    else if (MESSAGE_FLAG_TYPE(message->header.flags) == MESSAGE_FLAG_RESPONSE) {
        struct gracht_message_context*    entry      = hashtable_get(&client->messages, &(struct gracht_message_context) { .message_id = message->header.id });
        struct gracht_message_descriptor* descriptor = entry ? entry->internal_handle : NULL;
        if (!descriptor) {
            // what the heck?
            GRERROR(GRSTR("[gracht_client_wait_message] no-one was listening for message %u"), message->header.id);
            status = -1;
            goto listenOrExit;
        }
        
        // copy data over to message
        memcpy(&descriptor->message, message, message->header.length);
        
        // set status
        descriptor->status = GRACHT_MESSAGE_COMPLETED;

        // set message id handled
        messageId = message->header.id;
        
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
    
    if (!config || !config->link || !clientOut) {
        GRERROR(GRSTR("[gracht] [client] config or config link was null"));
        errno = EINVAL;
        return -1;
    }
    
    client = (gracht_client_t*)malloc(sizeof(gracht_client_t));
    if (!client) {
        GRERROR(GRSTR("gracht_client: failed to allocate memory for client data"));
        errno = (ENOMEM);
        return -1;
    }
    
    memset(client, 0, sizeof(gracht_client_t));
    mtx_init(&client->data_lock, mtx_plain);
    mtx_init(&client->wait_lock, mtx_plain);
    hashtable_construct(&client->protocols, 0, sizeof(struct gracht_protocol), protocol_hash, protocol_cmp);
    hashtable_construct(&client->messages, 0, sizeof(struct gracht_message_context), message_hash, message_cmp);
    hashtable_construct(&client->awaiters, 0, sizeof(struct gracht_message_awaiter), awaiter_hash, awaiter_cmp);

    client->ops = config->link;
    client->iod = GRACHT_CONN_INVALID;
    client->current_awaiter_id = 1;
    client->current_message_id = 1;

    client->max_message_size = config->max_message_size;
    if (client->max_message_size <= 0) {
        client->max_message_size = GRACHT_DEFAULT_MESSAGE_SIZE;
    }

    client->message_buffer = config->message_buffer;
    if (!client->message_buffer) {
        client->message_buffer = malloc(client->max_message_size);
        if (!client->message_buffer) {
            GRERROR(GRSTR("gracht_client: failed to allocate memory for message buffer"));
            errno = (ENOMEM);
            hashtable_destroy(&client->awaiters);
            hashtable_destroy(&client->messages);
            hashtable_destroy(&client->protocols);
            free(client);
            return -1;
        }
        client->free_buffer = 1;
    }

    // register the control protocol
    gracht_client_register_protocol(client, &control_protocol);
    
    *clientOut = client;
    return 0;
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

    client->iod = client->ops->connect(client->ops);
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
        client->ops->destroy(client->ops);
    }

    if (client->free_buffer) {
        free(client->message_buffer);
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

void gracht_control_error_event(gracht_client_t* client, struct gracht_control_error_event* event)
{
    struct gracht_message_context*    message    = hashtable_get(&client->messages, &(struct gracht_message_context) { .message_id = event->message_id });
    struct gracht_message_descriptor* descriptor = message ? message->internal_handle : NULL;
    if (!descriptor) {
        // what the heck?
        GRERROR(GRSTR("[gracht_control_error_event] no-one was listening for message %u"), event->message_id);
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
    const struct gracht_message_context* message = element;
    return message->message_id;
}

static int message_cmp(const void* element1, const void* element2)
{
    const struct gracht_message_context* message1 = element1;
    const struct gracht_message_context* message2 = element2;
    return message1->message_id == message2->message_id ? 0 : 1;
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
