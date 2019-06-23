
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

#ifndef _ESPGOODIES_PINGWDT_H
#define _ESPGOODIES_PINGWDT_H

#include <ip_addr.h>

#ifdef _DEBUG_PINGWDT
#define DEBUG_PINGWDT(fmt, ...)     DEBUG("[pingwdt       ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_PINGWDT(...)          {}
#endif

#define PING_WDT_ERR_COUNT          10  /* number of successive ping timeouts that trigger a reset */


ICACHE_FLASH_ATTR void              ping_wdt_init();
ICACHE_FLASH_ATTR void              ping_wdt_start(int interval);
ICACHE_FLASH_ATTR void              ping_wdt_stop();
ICACHE_FLASH_ATTR int               ping_wdt_get_interval();


#endif /* _ESPGOODIES_PINGWDT_H */
