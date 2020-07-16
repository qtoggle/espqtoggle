
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
 */

#ifndef _ESPGOODIES_PWM_H
#define _ESPGOODIES_PWM_H


#include <c_types.h>


#ifdef _DEBUG_PWM
#define DEBUG_PWM(fmt, ...) DEBUG("[pwm           ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_PWM(...)      {}
#endif


void   ICACHE_FLASH_ATTR pwm_setup(uint32 gpio_mask);
void   ICACHE_FLASH_ATTR pwm_enable(void);
void   ICACHE_FLASH_ATTR pwm_disable(void);

void   ICACHE_FLASH_ATTR pwm_set_duty(uint8 gpio_no, uint8 duty);
uint8  ICACHE_FLASH_ATTR pwm_get_duty(uint8 gpio_no);
void   ICACHE_FLASH_ATTR pwm_set_freq(uint32 freq);
uint32 ICACHE_FLASH_ATTR pwm_get_freq(void);


#endif /* _ESPGOODIES_PWM_H */
