
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

#ifndef _ESPGOODIES_RTC_H
#define _ESPGOODIES_RTC_H

#include <c_types.h>


#ifdef _DEBUG_RTC
#define DEBUG_RTC(fmt, ...)             DEBUG("[rtc           ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_RTC(...)                  {}
#endif


ICACHE_FLASH_ATTR void                  rtc_init();
ICACHE_FLASH_ATTR void                  rtc_reset();

ICACHE_FLASH_ATTR bool                  rtc_is_full_boot();
ICACHE_FLASH_ATTR int                   rtc_get_boot_count();

ICACHE_FLASH_ATTR int                   rtc_get_value(uint8 addr);
ICACHE_FLASH_ATTR void                  rtc_set_value(uint8 addr, int value);


#endif /* _ESPGOODIES_RTC_H */
