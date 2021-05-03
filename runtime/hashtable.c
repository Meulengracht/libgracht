/**
 * Copyright 2020, Philip Meulengracht
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
 * - Open addressed hashtable implementation using round robin for balancing.
 */

#include "hashtable.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <malloc.h>
#define alloca _alloca
#endif

struct hashtable_element {
    uint16_t probeCount;
    uint64_t hash;
    uint8_t  payload[];
};

#define SHOULD_GROW(hashtable)        (hashtable->element_count == hashtable->grow_count)
#define SHOULD_SHRINK(hashtable)      (hashtable->element_count == hashtable->shrink_count)

#define GET_ELEMENT_ARRAY(hashtable, elements, index) (struct hashtable_element*)&((uint8_t*)elements)[index * hashtable->element_size]
#define GET_ELEMENT(hashtable, index)                 GET_ELEMENT_ARRAY(hashtable, hashtable->elements, index)

static int  hashtable_resize(hashtable_t* hashtable, size_t newCapacity);
static void hashtable_remove_and_bump(hashtable_t* hashtable, struct hashtable_element* element, size_t index);

int hashtable_construct(hashtable_t* hashtable, size_t requestCapacity, size_t elementSize, hashtable_hashfn hashFunction, hashtable_cmpfn cmpFunction)
{
    size_t initialCapacity  = HASHTABLE_MINIMUM_CAPACITY;
    size_t totalElementSize = elementSize + sizeof(struct hashtable_element);
    void*  elementStorage;
    void*  swapElement;

    if (!hashtable || !hashFunction || !cmpFunction) {
        errno = EINVAL;
        return -1;
    }

    // select initial capacity
    if (requestCapacity > initialCapacity) {
        // Make sure we have a power of two
        while (initialCapacity < requestCapacity) {
            initialCapacity <<= 1;
        }
    }

    elementStorage = malloc(initialCapacity * totalElementSize);
    if (!elementStorage) {
        errno = ENOMEM;
        return -1;
    }
    memset(elementStorage, 0, initialCapacity * totalElementSize);

    swapElement = malloc(totalElementSize);
    if (!swapElement) {
        free(elementStorage);
        errno = ENOMEM;
        return -1;
    }

    hashtable->capacity      = initialCapacity;
    hashtable->element_count = 0;
    hashtable->grow_count    = (initialCapacity * HASHTABLE_LOADFACTOR_GROW) / 100;
    hashtable->shrink_count  = (initialCapacity * HASHTABLE_LOADFACTOR_SHRINK) / 100;
    hashtable->element_size  = totalElementSize;
    hashtable->elements      = elementStorage;
    hashtable->swap          = swapElement;
    hashtable->hash          = hashFunction;
    hashtable->cmp           = cmpFunction;
    return 0;
}

void hashtable_destroy(hashtable_t* hashtable)
{
    if (!hashtable) {
        return;
    }

    if (hashtable->swap) {
        free(hashtable->swap);
    }

    if (hashtable->elements) {
        free(hashtable->elements);
    }
}

void* hashtable_set(hashtable_t* hashtable, const void* element)
{
    if (!hashtable) {
        errno = EINVAL;
        return NULL;
    }

    uint8_t*                  elementBuffer = alloca(hashtable->element_size);
    struct hashtable_element* iterElement = (struct hashtable_element*)&elementBuffer[0];
    size_t                    index;

    // Only resize on entry - that way we avoid any unneccessary resizing
    if (SHOULD_GROW(hashtable) && hashtable_resize(hashtable, hashtable->capacity << 1)) {
        errno = ENOMEM;
        return NULL;
    }

    // build an intermediate object containing our new element
    iterElement->probeCount = 1;
    iterElement->hash       = hashtable->hash(element);
    memcpy(&iterElement->payload[0], element, hashtable->element_size - sizeof(struct hashtable_element));

    index = iterElement->hash & (hashtable->capacity - 1);
    while (1) {
        struct hashtable_element* current = GET_ELEMENT(hashtable, index);
        
        // few cases to consider when doing this, either the slot is not taken or it is
        if (!current->probeCount) {
            memcpy(current, iterElement, hashtable->element_size);
            hashtable->element_count++;
            return NULL;
        }
        else {
            // If the slot is taken, we either replace it or we move fit in between
            // Just because something shares hash these is no guarantee that it's an element we want
            // to replace - instead let the user decide. Another strategy here is to use double hashing
            // and try to trust that
            if (current->hash == iterElement->hash &&
                !hashtable->cmp(&current->payload[0], &iterElement->payload[0])) {
                memcpy(hashtable->swap, current, hashtable->element_size);
                memcpy(current, iterElement, hashtable->element_size);
                return &((struct hashtable_element*)hashtable->swap)->payload[0];
            }

            // ok so we instead insert it here if our probe count is lower, we should not stop
            // the iteration though, the element we swap out must be inserted again at the next
            // probe location, and we must continue this charade untill no more elements are displaced
            if (current->probeCount < iterElement->probeCount) {
                memcpy(hashtable->swap, current, hashtable->element_size);
                memcpy(current, iterElement, hashtable->element_size);
                memcpy(iterElement, hashtable->swap, hashtable->element_size);
            }
        }

        iterElement->probeCount++;
        index = (index + 1) & (hashtable->capacity - 1);
    }
}

void* hashtable_get(hashtable_t* hashtable, const void* key)
{
    uint64_t hash;
    size_t   index;

    if (!hashtable) {
        errno = EINVAL;
        return NULL;
    }

    hash  = hashtable->hash(key);
    index = hash & (hashtable->capacity - 1);
    while (1) {
        struct hashtable_element* current = GET_ELEMENT(hashtable, index);
        
        // termination condition
        if (!current->probeCount) {
            errno = ENOENT;
            return NULL;
        }

        // both hash and compare must match
        if (current->hash == hash && !hashtable->cmp(&current->payload[0], key)) {
            return &current->payload[0];
        }

        index = (index + 1) & (hashtable->capacity - 1);
    }
}

void* hashtable_remove(hashtable_t* hashtable, const void* key)
{
    uint64_t hash;
    size_t   index;

    if (!hashtable) {
        errno = EINVAL;
        return NULL;
    }

    // Only resize on entry to avoid any unncessary resizes
    if (SHOULD_SHRINK(hashtable) && hashtable_resize(hashtable, hashtable->capacity >> 1)) {
        errno = ENOMEM;
        return NULL;
    }

    hash  = hashtable->hash(key);
    index = hash & (hashtable->capacity - 1);
    while (1) {
        struct hashtable_element* current = GET_ELEMENT(hashtable, index);

        // Does the key event exist, otherwise exit
        if (!current->probeCount) {
            errno = ENOENT;
            return NULL;
        }

        if (current->hash == hash && !hashtable->cmp(&current->payload[0], key)) {
            hashtable_remove_and_bump(hashtable, current, index);
            return &((struct hashtable_element*)hashtable->swap)->payload[0];
        }

        index = (index + 1) & (hashtable->capacity - 1);
    }
}

void hashtable_enumerate(hashtable_t* hashtable, hashtable_enumfn enumFunction, void* context)
{
    size_t i;

    if (!hashtable || !enumFunction) {
        errno = EINVAL;
        return;
    }

    for (i = 0; i < hashtable->capacity; i++) {
        struct hashtable_element* current = GET_ELEMENT(hashtable, i);
        if (current->probeCount) {
            enumFunction((int)i, &current->payload[0], context);
        }
    }
}

static void hashtable_remove_and_bump(hashtable_t* hashtable, struct hashtable_element* element, size_t index)
{
    struct hashtable_element* previous = element;

    // Remove is a little bit more extensive, we have to bump up all elements that
    // share the hash
    memcpy(hashtable->swap, element, hashtable->element_size);
    element->probeCount = 0;

    index = (index + 1) & (hashtable->capacity - 1);
    while (1) {
        struct hashtable_element* current = GET_ELEMENT(hashtable, index);
        if (current->probeCount <= 1) {
            // this element is the first in a new chain or a free element
            break;
        }
        
        // reduce the probe count and move it one up
        current->probeCount--;
        memcpy(previous, current, hashtable->element_size);
        
        // store next space and move to next index
        previous = current;
        index    = (index + 1) & (hashtable->capacity - 1);
    }

    hashtable->element_count--;
}

static int hashtable_resize(hashtable_t* hashtable, size_t newCapacity)
{
    size_t updatedIndex;
    void*  resizedStorage;
    size_t i;

    // potentially there can be a too big resize - but practically very unlikely...
    if (newCapacity < HASHTABLE_MINIMUM_CAPACITY) {
        return 0; // ignore resize
    }

    resizedStorage = malloc(newCapacity * hashtable->element_size);
    if (!resizedStorage) {
        errno = ENOMEM;
        return -1;
    }
    
    // transfer objects and reset their probeCount
    memset(resizedStorage, 0, newCapacity * hashtable->element_size);
    for (i = 0; i < hashtable->capacity; i++) {
        struct hashtable_element* current = GET_ELEMENT(hashtable, i);
        struct hashtable_element* newCurrent;
        if (current->probeCount) {
            current->probeCount = 1;
            updatedIndex        = current->hash & (newCapacity - 1);
            
            while (1) {
                newCurrent = GET_ELEMENT_ARRAY(hashtable, resizedStorage, updatedIndex);
                if (!newCurrent->probeCount) {
                    memcpy(newCurrent, current, hashtable->element_size);
                    break;
                }
                else {
                    if (newCurrent->probeCount < current->probeCount) {
                        memcpy(hashtable->swap, newCurrent, hashtable->element_size);
                        memcpy(newCurrent, current, hashtable->element_size);
                        memcpy(current, hashtable->swap, hashtable->element_size);
                    }
                }

                current->probeCount++;
                updatedIndex = (updatedIndex + 1) & (newCapacity - 1);
            }
        }
    }

    free(hashtable->elements);

    hashtable->elements      = resizedStorage;
    hashtable->capacity      = newCapacity;
    hashtable->grow_count    = (newCapacity * HASHTABLE_LOADFACTOR_GROW) / 100;
    hashtable->shrink_count  = (newCapacity * HASHTABLE_LOADFACTOR_SHRINK) / 100;

    return 0;
}
