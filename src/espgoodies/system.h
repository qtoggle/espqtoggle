
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

#define DEFAULT_HOSTNAME        "esp-%08x"

#define MAX_AVAILABLE_RAM       (40 * 1024) /* 40k is an upper limit to the available RAM */


/* Following items go into system flash configuration */

#define SYSTEM_CONFIG_OFFS_SETUP_BUTTON_PIN     0x0000 /*    1 byte */
#define SYSTEM_CONFIG_OFFS_SETUP_BUTTON_LEVEL   0x0001 /*    1 byte */
#define SYSTEM_CONFIG_OFFS_SETUP_BUTTON_HOLD    0x0002 /*    1 byte */
#define SYSTEM_CONFIG_OFFS_SETUP_BUTTON_HOLDR   0x0003 /*    1 byte */

#define SYSTEM_CONFIG_OFFS_STATUS_LED_PIN       0x0004 /*    1 byte */
#define SYSTEM_CONFIG_OFFS_STATUS_LED_LEVEL     0x0005 /*    1 byte */

#define SYSTEM_CONFIG_OFFS_BATTERY_DIV_FACTOR   0x0006 /*    2 bytes */
#define SYSTEM_CONFIG_OFFS_BATTERY_VOLTAGES     0x0008 /*   12 bytes */


typedef void (*system_reset_callback_t)(void);
typedef void (*system_setup_mode_callback_t)(bool active);


ICACHE_FLASH_ATTR void          system_init(void);
ICACHE_FLASH_ATTR void          system_save(void);

ICACHE_FLASH_ATTR uint32        system_uptime(void);
ICACHE_FLASH_ATTR uint64        system_uptime_ms(void);
uint64                          system_uptime_us(void); /* Needs to be called at least once every hour */

ICACHE_FLASH_ATTR int           system_get_flash_size(void);

ICACHE_FLASH_ATTR void          system_reset(bool delayed);
ICACHE_FLASH_ATTR void          system_reset_set_callback(system_reset_callback_t callback);

ICACHE_FLASH_ATTR void          system_setup_button_configure(int8 pin, bool level, uint8 hold, uint8 reset_hold);
ICACHE_FLASH_ATTR void          system_setup_button_get_config(int8 *pin, bool *level, uint8 *hold,
                                                                      uint8 *reset_hold);
ICACHE_FLASH_ATTR void          system_status_led_configure(int8 pin, bool level);
ICACHE_FLASH_ATTR void          system_status_led_get_config(int8 *pin, bool *level);

ICACHE_FLASH_ATTR void          system_setup_mode_set_callback(system_setup_mode_callback_t callback);
ICACHE_FLASH_ATTR bool          system_setup_mode_active(void);
ICACHE_FLASH_ATTR void          system_setup_mode_toggle(void);

ICACHE_FLASH_ATTR void          system_update(void);

#endif /* _ESPGOODIES_SYSTEM_H */
