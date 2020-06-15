
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

#ifndef _PORTS_VIRTUAL_H
#define _PORTS_VIRTUAL_H

#include <c_types.h>

#include "ports.h"

#define VIRTUAL_MAX_PORTS           8

#ifdef _DEBUG_VIRTUAL
#define DEBUG_VIRTUAL(fmt, ...)     DEBUG("[virtual       ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_VIRTUAL(...)          {}
#endif


ICACHE_FLASH_ATTR void              virtual_ports_init(uint8 *data);
ICACHE_FLASH_ATTR void              virtual_ports_save(uint8 *data, uint32 *strings_offs);
ICACHE_FLASH_ATTR int8              virtual_find_unused_slot(bool occupy);
ICACHE_FLASH_ATTR bool              virtual_port_register(port_t *port);
ICACHE_FLASH_ATTR bool              virtual_port_unregister(port_t *port);


#endif  /* _PORTS_VIRTUAL_H */
