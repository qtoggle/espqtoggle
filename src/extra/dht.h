
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

/*
 * DHT11/DHT22 temperature/humidity sensor ports.
 */

#ifndef _EXTRA_DHT_H
#define _EXTRA_DHT_H

#include <ets_sys.h>

#include "ports.h"


#ifdef _DEBUG_DHT
#define DEBUG_DHT(port, f, ...)     DEBUG("[dht.%-10s] " f, (port)->id, ##__VA_ARGS__)
#else
#define DEBUG_DHT(...)              {}
#endif

#ifdef HAS_DHT
extern port_t                     * temperature0;
extern port_t                     * humidity0;
#endif

ICACHE_FLASH_ATTR void              dht_init_ports();


#endif /* _EXTRA_DHT_H */
