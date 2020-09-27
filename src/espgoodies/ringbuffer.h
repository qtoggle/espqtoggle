
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

#ifndef _ESPGOODIES_RING_BUFFER_H
#define _ESPGOODIES_RING_BUFFER_H


#include <c_types.h>


typedef struct {

    uint8  *data;
    uint16  size;
    uint16  avail;
    uint16  head;
    uint16  tail;

} ring_buffer_t;


ring_buffer_t ICACHE_FLASH_ATTR *ring_buffer_new(uint16 size);
void          ICACHE_FLASH_ATTR  ring_buffer_free(ring_buffer_t *ring_buffer);
uint16        ICACHE_FLASH_ATTR  ring_buffer_peek(ring_buffer_t *ring_buffer, uint8 *data, uint16 len);
uint16        ICACHE_FLASH_ATTR  ring_buffer_read(ring_buffer_t *ring_buffer, uint8 *data, uint16 len);
/* Intentionally not placed into flash so that it can be used from UART interrupt handler */
uint16                           ring_buffer_write(ring_buffer_t *ring_buffer, uint8 *data, uint16 len);


#endif /* _ESPGOODIES_RING_BUFFER_H */
