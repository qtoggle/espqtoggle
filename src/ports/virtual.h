
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

#ifndef _PORTS_VIRTUAL_H
#define _PORTS_VIRTUAL_H

#include <c_types.h>

#include "ports.h"

#define VIRTUAL0_SLOT               24
#define VIRTUAL_MAX_PORTS           8

#ifdef _DEBUG_VIRTUAL
#define DEBUG_VIRTUAL(fmt, ...)     DEBUG("[virtual       ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_VIRTUAL(...)          {}
#endif


ICACHE_FLASH_ATTR void              virtual_init_ports(uint8 *data);
ICACHE_FLASH_ATTR int8              virtual_find_unused_slot(bool occupy);
ICACHE_FLASH_ATTR bool              virtual_port_register(port_t *port);
ICACHE_FLASH_ATTR bool              virtual_port_unregister(port_t *port);


#endif  /* _PORTS_VIRTUAL_H */
