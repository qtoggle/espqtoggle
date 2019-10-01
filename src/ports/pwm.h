
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

#ifndef _PORTS_PWM_H
#define _PORTS_PWM_H

#include <c_types.h>

#include "ports.h"


#if defined(HAS_PWM0) || defined(HAS_PWM1)  || defined(HAS_PWM2)  || defined(HAS_PWM3)  || defined(HAS_PWM4) || \
    defined(HAS_PWM5) || defined(HAS_PWM12) || defined(HAS_PWM13) || defined(HAS_PWM14) || defined(HAS_PWM15)
#define HAS_PWM
#endif


#ifdef _DEBUG_PWM
#define DEBUG_PWM(port, f, ...) DEBUG("[%-14s] " f, (port)->id, ##__VA_ARGS__)
#else
#define DEBUG_PWM(...)          {}
#endif

#ifdef HAS_PWM0
extern port_t *                 pwm0;
#endif
#ifdef HAS_PWM1
extern port_t *                 pwm1;
#endif
#ifdef HAS_PWM2
extern port_t *                 pwm2;
#endif
#ifdef HAS_PWM3
extern port_t *                 pwm3;
#endif
#ifdef HAS_PWM4
extern port_t *                 pwm4;
#endif
#ifdef HAS_PWM5
extern port_t *                 pwm5;
#endif
#ifdef HAS_PWM12
extern port_t *                 pwm12;
#endif
#ifdef HAS_PWM13
extern port_t *                 pwm13;
#endif
#ifdef HAS_PWM14
extern port_t *                 pwm14;
#endif
#ifdef HAS_PWM15
extern port_t *                 pwm15;
#endif


ICACHE_FLASH_ATTR void          pwm_init_ports(void);


#endif  /* _PORTS_PWM_H */
