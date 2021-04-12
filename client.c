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
#include "include/list.h"
#include "include/hashtable.h"
#include "include/debug.h"
#include "include/thread_api.h"
#include "include/control.h"
#include "include/utils.h"
#include <string.h>
#include <stdlib.h>

struct gracht_message_awaiter {
    struct gracht_object_header header;
    unsigned int                flags;
    cnd_t                       event;
    int                         id_count;
    uint32_t                    ids[];
};

// descriptor | message | params
struct gracht_message_descriptor {
    gracht_object_header_t header;
    int                    status;
    struct gracht_message  message;
};

typedef struct gracht_client {
    int                     iod;
    uint32_t                current_message_id;
    struct client_link_ops* ops;
    hashtable_t             protocols;
    struct gracht_list      awaiters;
    struct gracht_list      messages;
    mtx_t                   sync_object;
    mtx_t                   wait_object;
} gracht_client_t;

// static methods
static uint32_t get_message_id(gracht_client_t*);
static void     mark_awaiters(gracht_client_t*, uint32_t);
static int      check_awaiter_condition(gracht_client_t*, struct gracht_message_awaiter*, struct gracht_message_context**, int);

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
        
        context->message_id = message->header.id;
        context->descriptor = malloc(bufferLength);
        if (!context->descriptor) {
            errno = ENOMEM;
            return -1;
        }
        
        descriptor = context->descriptor;
        descriptor->header.id   = (int)message->header.id;
        descriptor->header.link = NULL;
        descriptor->status      = GRACHT_MESSAGE_CREATED;
        
        mtx_lock(&client->sync_object);
        gracht_list_append(&client->messages, &descriptor->header);
        mtx_unlock(&client->sync_object);
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
    struct gracht_message_awaiter* awaiter;
    int                            i;
    
    if (!client || !contexts || !contextCount) {
        errno = (EINVAL);
        return -1;
    }
    
    awaiter = malloc(sizeof(struct gracht_message_awaiter) + (sizeof(uint32_t) * contextCount));
    if (!awaiter) {
        errno = (ENOMEM);
        return -1;
    }
    
    cnd_init(&awaiter->event);
    awaiter->header.id   = 0;
    awaiter->header.link = NULL;
    awaiter->flags       = flags;
    awaiter->id_count    = contextCount;
    for (i = 0; i < contextCount; i++) {
        awaiter->ids[i] = contexts[i]->message_id;
    }
    
    // do not add the awaiter if the condition is success
    mtx_lock(&client->sync_object);
    if (check_awaiter_condition(client, awaiter, contexts, contextCount)) {
        gracht_list_append(&client->awaiters, &awaiter->header);
        cnd_wait(&awaiter->event, &client->sync_object);
        gracht_list_remove(&client->awaiters, &awaiter->header);
    }
    mtx_unlock(&client->sync_object);
    
    free(awaiter);
    return 0;
}

int gracht_client_await(gracht_client_t* client, struct gracht_message_context* context)
{
    return gracht_client_await_multiple(client, &context, 1, GRACHT_AWAIT_ANY);
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
    mtx_lock(&client->sync_object);
    descriptor = (struct gracht_message_descriptor*)context->descriptor;
    if (!descriptor) {
        mtx_unlock(&client->sync_object);

        GRERROR(GRSTR("[gracht] [client] descriptor for message was not found"));
        errno = (EALREADY);
        return -1;
    }
    
    if (descriptor->status == GRACHT_MESSAGE_COMPLETED || descriptor->status == GRACHT_MESSAGE_ERROR) {
        gracht_list_remove(&client->messages, &descriptor->header);
    }
    mtx_unlock(&client->sync_object);

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
        free(context->descriptor);
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
        void*                          messageBuffer,
        unsigned int                   flags)
{
    struct gracht_message* message;
    int                    status;
    uint32_t               messageId = 0;
    
    if (!client || !messageBuffer) {
        errno = (EINVAL);
        return -1;
    }

    // We must acquire hold of the client mutex as the mutex must be thread-safe in case
    // of multiple calling threads. Only one thread can listen for client events at a time, and that
    // means all other threads must queue up and wait for the thread that listens for events to signal
    // them. This means the listening thread is now the orchestrator for the rest.
listenForMessage:
    if (mtx_trylock(&client->wait_object) != thrd_success) {
        if (!(flags & GRACHT_MESSAGE_BLOCK)) {
            errno = EBUSY;
            return -1;
        }

        // did not succeed in taking the lock, if we are waiting for a specific message, then we do an
        // await instead
        if (context) {
            return gracht_client_await(client, context);
        }
        else {
            // otherwise we are trying to just wait for events, and another thread is already doing that
            // should we just wait? For now accept the fate that we wait
            if (mtx_lock(&client->wait_object) != thrd_success) {
                return -1;
            }
        }
    }

    status = client->ops->recv(client->ops, messageBuffer, flags, &message);
    mtx_unlock(&client->wait_object);
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
        struct gracht_message_descriptor* descriptor = (struct gracht_message_descriptor*)
            gracht_list_lookup(&client->messages, (int)message->header.id);
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
        mtx_lock(&client->sync_object);
        mark_awaiters(client, message->header.id);
        mtx_unlock(&client->sync_object);
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
    mtx_init(&client->sync_object, mtx_plain);
    mtx_init(&client->wait_object, mtx_plain);
    hashtable_construct(&client->protocols, 0, sizeof(struct gracht_protocol), protocol_hash, protocol_cmp);

    client->ops = config->link;
    client->iod = client->ops->connect(client->ops);
    if (client->iod < 0) {
        GRERROR(GRSTR("gracht_client: failed to connect client"));
        gracht_client_shutdown(client);
        return -1;
    }

    // register the control protocol
    gracht_client_register_protocol(client, &control_protocol);
    
    *clientOut = client;
    return 0;
}

void gracht_client_shutdown(gracht_client_t* client)
{
    if (!client) {
        return;
    }
    
    if (client->iod > 0) {
        client->ops->destroy(client->ops);
    }
    
    hashtable_destroy(&client->protocols);
    mtx_destroy(&client->sync_object);
    mtx_destroy(&client->wait_object);
    free(client);
}

int gracht_client_iod(gracht_client_t* client)
{
    if (!client) {
        errno = EINVAL;
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
        return;
    }
    
    hashtable_remove(&client->protocols, protocol);
}

static void mark_awaiters(gracht_client_t* client, uint32_t messageId)
{
    struct gracht_object_header* item;
    
    item = GRACHT_LIST_HEAD(&client->awaiters);
    while (item) {
        struct gracht_message_awaiter* awaiter = 
            (struct gracht_message_awaiter*)item;
        int idsLeft = awaiter->id_count;
        int i;
        
        for (i = 0; i < awaiter->id_count; i++) {
            if (awaiter->ids[i] == 0) {
                idsLeft--;
                continue;
            }
            
            if (awaiter->ids[i] == messageId) {
                awaiter->ids[i] = 0;
                idsLeft--;
            }
        }
        
        if (idsLeft != awaiter->id_count) {
            if (idsLeft == 0 || awaiter->flags == GRACHT_AWAIT_ANY) {
                cnd_signal(&awaiter->event);
            }
        }
        
        item = GRACHT_LIST_LINK(item);
    }
}

static int check_awaiter_condition(gracht_client_t* client,
    struct gracht_message_awaiter* awaiter, struct gracht_message_context** contexts,
    int contextCount)
{
    int messagesCompleted = 0;
    int i;
    
    for (i = 0; i < contextCount; i++) {
        struct gracht_message_descriptor* descriptor =  (struct gracht_message_descriptor*)
            gracht_list_lookup(&client->messages, (int)contexts[i]->message_id);
        if (descriptor && ( 
                descriptor->status == GRACHT_MESSAGE_INPROGRESS ||
                descriptor->status == GRACHT_MESSAGE_CREATED)) {
            continue;
        }
        
        messagesCompleted++;
    }
    
    if (messagesCompleted != 0) {
        if (messagesCompleted == contextCount || awaiter->flags == GRACHT_AWAIT_ANY) {
            // condition was met
            return 0;
        }
    }
    return -1;
}

static uint32_t get_message_id(gracht_client_t* client)
{
    return client->current_message_id++;
}

void gracht_control_error_event(gracht_client_t* client, struct gracht_control_error_event* event)
{
    struct gracht_message_descriptor* descriptor = (struct gracht_message_descriptor*)
        gracht_list_lookup(&client->messages, (int)event->message_id);
    if (!descriptor) {
        // what the heck?
        GRERROR(GRSTR("[gracht_control_error_event] no-one was listening for message %u"), event->message_id);
        return;
    }
    
    // set status
    descriptor->status = GRACHT_MESSAGE_ERROR;
    
    // iterate awaiters and mark those that contain this message
    mtx_lock(&client->sync_object);
    mark_awaiters(client, event->message_id);
    mtx_unlock(&client->sync_object);
}
