
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

#ifndef _ESPGOODIES_BATTERY_H
#define _ESPGOODIES_BATTERY_H


#include <c_types.h>


#ifdef _DEBUG_BATTERY
#define DEBUG_BATTERY(f, ...) DEBUG("[battery       ] " f, ##__VA_ARGS__)

#else
#define DEBUG_BATTERY(...)    {}
#endif


#define BATTERY_LUT_LEN 6


void   ICACHE_FLASH_ATTR battery_config_load(uint8 *config_data);
void   ICACHE_FLASH_ATTR battery_config_save(uint8 *config_data);

void   ICACHE_FLASH_ATTR battery_configure(uint16 div_factor, uint16 voltages[]);
void   ICACHE_FLASH_ATTR battery_get_config(uint16 *div_factor, uint16 *voltages);

bool   ICACHE_FLASH_ATTR battery_enabled(void);

uint16 ICACHE_FLASH_ATTR battery_get_voltage(void); /* Millivolts */
uint8  ICACHE_FLASH_ATTR battery_get_level(void);   /* Percent, 0 - 100 */


#endif  /* _ESPGOODIES_BATTERY_H */
