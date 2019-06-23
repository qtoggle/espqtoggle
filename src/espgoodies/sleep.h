
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

#ifndef _ESPGOODIES_SLEEP_H
#define _ESPGOODIES_SLEEP_H


#include <c_types.h>


#ifdef _DEBUG_SLEEP
#define DEBUG_SLEEP(fmt, ...)           DEBUG("[sleep         ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_SLEEP(...)                {}
#endif

#define SLEEP_WAKE_INTERVAL_MIN         0           /* 0 - sleep disabled */
#define SLEEP_WAKE_INTERVAL_MAX         (7 * 1440)  /* minutes */
#define SLEEP_WAKE_DURATION_MIN         1           /* seconds */
#define SLEEP_WAKE_DURATION_MAX         3600        /* seconds */
#define SLEEP_WAKE_CONNECT_TIMEOUT      20          /* seconds */


ICACHE_FLASH_ATTR void                  sleep_init();
ICACHE_FLASH_ATTR void                  sleep_reset();
ICACHE_FLASH_ATTR bool                  sleep_pending();
ICACHE_FLASH_ATTR bool                  sleep_is_short_wake();

ICACHE_FLASH_ATTR int                   sleep_get_wake_interval();
ICACHE_FLASH_ATTR void                  sleep_set_wake_interval(int interval);

ICACHE_FLASH_ATTR int                   sleep_get_wake_duration();
ICACHE_FLASH_ATTR void                  sleep_set_wake_duration(int duration);


#endif /* _ESPGOODIES_SLEEP_H */
