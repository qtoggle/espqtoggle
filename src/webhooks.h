
/*
 * Copyright (c) Calin Crisan
 * This file is part of espQToggle.
 *
 * espQToggle is free software: you can redistribute it and/or modify
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

#ifndef _WEBHOOKS_H
#define _WEBHOOKS_H


#include "espgoodies/json.h"

#include "ports.h"


#ifdef _DEBUG_WEBHOOKS
#define DEBUG_WEBHOOKS(fmt, ...)            DEBUG("[webhooks      ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_WEBHOOKS(...)                 {}
#endif

#define WEBHOOKS_MIN_TIMEOUT                1
#define WEBHOOKS_MAX_TIMEOUT                3600
#define WEBHOOKS_DEF_TIMEOUT                60

#define WEBHOOKS_MIN_RETRIES                0
#define WEBHOOKS_MAX_RETRIES                10

#define WEBHOOKS_MAX_QUEUE_LEN              8


extern char                               * webhooks_host;
extern uint16                               webhooks_port;
extern char                               * webhooks_path;
extern char                                 webhooks_password_hash[];
extern uint8                                webhooks_events_mask;
extern int                                  webhooks_timeout;
extern int                                  webhooks_retries;


ICACHE_FLASH_ATTR void                      webhooks_push_event(int type, json_t *params, port_t *port);


#endif /* _WEBHOOKS_H */

