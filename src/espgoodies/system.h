
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

#ifndef _ESPGOODIES_SYSTEM_H
#define _ESPGOODIES_SYSTEM_H


#include <os_type.h>


#ifdef _DEBUG_SYSTEM
#define DEBUG_SYSTEM(fmt, ...)  DEBUG("[system        ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_SYSTEM(...)       {}
#endif


typedef void (*system_reset_callback_t)();


extern int8                     system_setup_mode_gpio_no;
extern int8                     system_setup_mode_led_gpio_no;
extern bool                     system_setup_mode_level;
extern int32                    system_setup_mode_int;
extern int32                    system_setup_mode_reset_int;
extern int8                     system_connected_led_gpio_no;
extern bool                     system_connected_led_level;


ICACHE_FLASH_ATTR void          system_init();

ICACHE_FLASH_ATTR uint32        system_uptime(); /* needs to be called at least once every half an hour */
ICACHE_FLASH_ATTR uint64        system_uptime_us();

ICACHE_FLASH_ATTR int           system_get_flash_size();

ICACHE_FLASH_ATTR void          system_reset(bool delayed);
ICACHE_FLASH_ATTR void          system_set_reset_callback(system_reset_callback_t callback);

ICACHE_FLASH_ATTR bool          system_setup_mode_active();
ICACHE_FLASH_ATTR void          system_setup_mode_toggle();
ICACHE_FLASH_ATTR void          system_setup_mode_update();

ICACHE_FLASH_ATTR void          system_connected_led_update();


#endif /* _ESPGOODIES_SYSTEM_H */
