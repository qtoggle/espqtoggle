
/*
 * Copyright (c) Calin Crisan
 * This file is part of espqToggle.
 *
 * espqToggle is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>. 
 */

#ifndef _EVENTS_H
#define _EVENTS_H


#define EVENT_TYPE_MIN              1
#define EVENT_TYPE_VALUE_CHANGE     1
#define EVENT_TYPE_PORT_UPDATE      2
#define EVENT_TYPE_PORT_ADD         3
#define EVENT_TYPE_PORT_REMOVE      4
#define EVENT_TYPE_DEVICE_UPDATE    5
#define EVENT_TYPE_MAX              5


#include "espgoodies/json.h"

#include "ports.h"


typedef struct {

    int8                            type;
    json_t                        * params;
    port_t                        * port; /* used as port cache */

} event_t;


extern char *                       EVENT_TYPES_STR[];
extern int                          EVENT_ACCESS_LEVELS[];


ICACHE_FLASH_ATTR void              event_push(int type, json_t *params, port_t *port);
ICACHE_FLASH_ATTR void              event_push_value_change(port_t *port);
ICACHE_FLASH_ATTR void              event_push_port_update(port_t *port);
ICACHE_FLASH_ATTR void              event_push_port_add(port_t *port);
ICACHE_FLASH_ATTR void              event_push_port_remove(char *port_id);
ICACHE_FLASH_ATTR void              event_push_device_update(void);
ICACHE_FLASH_ATTR json_t *          event_to_json(event_t *event);
ICACHE_FLASH_ATTR void              event_free(event_t *event);


#endif /* _EVENTS_H */
