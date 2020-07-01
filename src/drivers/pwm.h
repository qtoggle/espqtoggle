
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

#ifndef _PWM_H
#define _PWM_H


#include <c_types.h>

#include "espgoodies/common.h"


#ifdef _DEBUG_PWM
#define DEBUG_PWM(fmt, ...) DEBUG("[pwm           ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_PWM(...)      {}
#endif


ICACHE_FLASH_ATTR void   pwm_setup(uint32 gpio_mask);
ICACHE_FLASH_ATTR void   pwm_enable(void);
ICACHE_FLASH_ATTR void   pwm_disable(void);

ICACHE_FLASH_ATTR void   pwm_set_duty(uint8 gpio_no, uint8 duty);
ICACHE_FLASH_ATTR uint8  pwm_get_duty(uint8 gpio_no);
ICACHE_FLASH_ATTR void   pwm_set_freq(uint32 freq);
ICACHE_FLASH_ATTR uint32 pwm_get_freq(void);


#endif /* _PWM_H */
