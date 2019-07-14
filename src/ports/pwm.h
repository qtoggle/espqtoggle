
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
