
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

#ifndef _EVENTS_H
#define _EVENTS_H


#define EVENT_TYPE_MIN           1
#define EVENT_TYPE_VALUE_CHANGE  1
#define EVENT_TYPE_PORT_UPDATE   2
#define EVENT_TYPE_PORT_ADD      3
#define EVENT_TYPE_PORT_REMOVE   4
#define EVENT_TYPE_DEVICE_UPDATE 5
#define EVENT_TYPE_FULL_UPDATE   6
#define EVENT_TYPE_MAX           6


#include "espgoodies/json.h"

#include "ports.h"
#include "jsonrefs.h"


#ifdef _DEBUG_EVENTS
#define DEBUG_EVENTS(fmt, ...) DEBUG("[events        ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_EVENTS(...)      {}
#endif


typedef struct {

    int8  type;
    char *port_id;

} event_t;


extern char *EVENT_TYPES_STR[];
extern int   EVENT_ACCESS_LEVELS[];


ICACHE_FLASH_ATTR void    event_push_value_change(port_t *port);
ICACHE_FLASH_ATTR void    event_push_port_update(port_t *port);
ICACHE_FLASH_ATTR void    event_push_port_add(port_t *port);
ICACHE_FLASH_ATTR void    event_push_port_remove(port_t *port);
ICACHE_FLASH_ATTR void    event_push_device_update(void);
ICACHE_FLASH_ATTR void    event_push_full_update(void);
ICACHE_FLASH_ATTR json_t *event_to_json(event_t *event, json_refs_ctx_t *json_refs_ctx);
ICACHE_FLASH_ATTR void    event_free(event_t *event);


#endif /* _EVENTS_H */
