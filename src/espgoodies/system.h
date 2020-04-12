
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

#ifndef _ESPGOODIES_SYSTEM_H
#define _ESPGOODIES_SYSTEM_H


#include <os_type.h>


#ifdef _DEBUG_SYSTEM
#define DEBUG_SYSTEM(fmt, ...)  DEBUG("[system        ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_SYSTEM(...)       {}
#endif

#define DEFAULT_HOSTNAME        "esp%08x"

#define MAX_AVAILABLE_RAM       (40 * 1024) /* 40k is an upper limit to the available RAM */


typedef void (*system_reset_callback_t)(void);


extern int8                     system_setup_mode_gpio_no;
extern int8                     system_setup_mode_led_gpio_no;
extern int32                    system_setup_mode_int;
extern int32                    system_setup_mode_reset_int;
extern int8                     system_connected_led_gpio_no;


ICACHE_FLASH_ATTR void          system_init(void);

ICACHE_FLASH_ATTR uint32        system_uptime(void); /* Needs to be called at least once every half an hour */
ICACHE_FLASH_ATTR uint64        system_uptime_ms(void);
ICACHE_FLASH_ATTR uint64        system_uptime_us(void);

ICACHE_FLASH_ATTR int           system_get_flash_size(void);

ICACHE_FLASH_ATTR void          system_reset(bool delayed);
ICACHE_FLASH_ATTR void          system_set_reset_callback(system_reset_callback_t callback);

ICACHE_FLASH_ATTR bool          system_setup_mode_active(void);
ICACHE_FLASH_ATTR void          system_setup_mode_toggle(void);
ICACHE_FLASH_ATTR void          system_setup_mode_update(void);

ICACHE_FLASH_ATTR void          system_connected_led_update(void);


#endif /* _ESPGOODIES_SYSTEM_H */
