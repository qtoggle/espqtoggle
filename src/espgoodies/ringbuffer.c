
/*
 * Copyright 2019 The qToggle Team
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <mem.h>

#include "espgoodies/common.h"
#include "espgoodies/utils.h"
#include "espgoodies/ringbuffer.h"


static uint16 ICACHE_FLASH_ATTR ring_buffer_read_peek(ring_buffer_t *ring_buffer, uint8 *data, uint16 len, bool peek);


ring_buffer_t *ring_buffer_new(uint16 size) {
    ring_buffer_t *ring_buffer = zalloc(sizeof(ring_buffer_t));

    ring_buffer->data = malloc(size);
    ring_buffer->size = size;

    return ring_buffer;
}

void ring_buffer_free(ring_buffer_t *ring_buffer) {
    free(ring_buffer->data);
    free(ring_buffer);
}

uint16 ring_buffer_peek(ring_buffer_t *ring_buffer, uint8 *data, uint16 len) {
    return ring_buffer_read_peek(ring_buffer, data, len, /* peek = */ TRUE);
}

uint16 ring_buffer_read(ring_buffer_t *ring_buffer, uint8 *data, uint16 len) {
    return ring_buffer_read_peek(ring_buffer, data, len, /* peek = */ FALSE);
}

uint16 ring_buffer_write(ring_buffer_t *ring_buffer, uint8 *data, uint16 len) {
    uint16 len1 = 0, len2 = 0;
    uint16 avail_len1, avail_len2;

    if (ring_buffer->tail <= ring_buffer->head) {
        if (ring_buffer->tail == ring_buffer->head && ring_buffer->avail) { /* Buffer full */
            return 0;
        }

        avail_len1 = ring_buffer->size - ring_buffer->head;
        avail_len2 = ring_buffer->tail;

        if (avail_len1 >= len) { /* Data from head side is enough */
            len1 = len;
        }
        else { /* We need to read data from tail side as well */
            len1 = avail_len1;
            len2 = MIN(avail_len2, len - avail_len1);
        }

        memcpy(ring_buffer->data + ring_buffer->head, data, len1);
        if (len2 > 0) {
            memcpy(ring_buffer->data, data + len1, len2);
        }

        ring_buffer->head += len1 + len2;
        if (ring_buffer->head >= ring_buffer->size) {
            ring_buffer->head = len2;
        }
    }
    else { /* head > tail */
        avail_len1 = ring_buffer->tail - ring_buffer->head;
        len1 = MIN(len, avail_len1);
        memcpy(ring_buffer->data + ring_buffer->head, data, len1);
        ring_buffer->head += len1;
    }

    ring_buffer->avail += len1 + len2;

    return len1 + len2;
}

uint16 ring_buffer_read_peek(ring_buffer_t *ring_buffer, uint8 *data, uint16 len, bool peek) {
    uint16 len1 = 0, len2 = 0;
    uint16 avail_len1, avail_len2;

    if (ring_buffer->tail < ring_buffer->head) {
        avail_len1 = ring_buffer->head - ring_buffer->tail;
        len1 = MIN(len, avail_len1);
        memcpy(data, ring_buffer->data + ring_buffer->tail, len1);
        if (!peek) {
            ring_buffer->tail += len1;
        }
    }
    else { /* head > tail */
        if (ring_buffer->tail == ring_buffer->head && !ring_buffer->avail) { /* Buffer empty */
            return 0;
        }

        avail_len1 = ring_buffer->size - ring_buffer->tail;
        avail_len2 = ring_buffer->head;

        if (avail_len1 >= len) { /* Data from tail side is enough */
            len1 = len;
        }
        else { /* We need to read data from head side as well */
            len1 = avail_len1;
            len2 = MIN(avail_len2, len - avail_len1);
        }

        memcpy(data, ring_buffer->data + ring_buffer->tail, len1);
        if (len2 > 0) {
            memcpy(data + len1, ring_buffer->data, len2);
        }

        if (!peek) {
            ring_buffer->tail += len1 + len2;
            if (ring_buffer->tail >= ring_buffer->size) {
                ring_buffer->tail = len2;
            }
        }
    }

    if (!peek) {
        ring_buffer->avail -= len1 + len2;
    }

    return len1 + len2;
}
