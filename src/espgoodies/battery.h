
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

#ifndef _ESPGOODIES_BATTERY_H
#define _ESPGOODIES_BATTERY_H


#include <c_types.h>


#ifdef _DEBUG_BATTERY
#define DEBUG_BATTERY(f, ...)       DEBUG("[battery       ] " f, ##__VA_ARGS__)

#else
#define DEBUG_BATTERY(...)          {}
#endif


ICACHE_FLASH_ATTR int               battery_get_voltage();  /* millivolts */
ICACHE_FLASH_ATTR int               battery_get_level();    /* percent, 0 - 100 */


#endif  /* _ESPGOODIES_BATTERY_H */
