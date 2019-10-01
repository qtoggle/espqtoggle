
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

