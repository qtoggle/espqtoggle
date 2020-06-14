
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

#ifndef _CORE_H
#define _CORE_H


#include <c_types.h>

#include "sessions.h"
#include "ports.h"


#ifdef _DEBUG_CORE
#define DEBUG_CORE(fmt, ...) DEBUG("[core          ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_CORE(...)      {}
#endif

#define TIME_EXPR_DEP_BIT    63  /* Used in change masks */
#define TIME_MS_EXPR_DEP_BIT 62  /* Used in change masks */


void ICACHE_FLASH_ATTR core_init(void);
void ICACHE_FLASH_ATTR core_listen_respond(session_t *session);

void ICACHE_FLASH_ATTR core_enable_polling(void);
void ICACHE_FLASH_ATTR core_disable_polling(void);
void ICACHE_FLASH_ATTR core_poll(void);

void ICACHE_FLASH_ATTR update_port_expression(port_t *port);
void ICACHE_FLASH_ATTR config_mark_for_saving(void);
void ICACHE_FLASH_ATTR config_ensure_saved(void);


#endif /* _CORE_H */
