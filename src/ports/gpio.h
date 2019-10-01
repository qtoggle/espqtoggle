
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

#ifndef _PORTS_GPIO_H
#define _PORTS_GPIO_H

#include <c_types.h>

#include "ports.h"


#if defined(HAS_GPIO0)  || defined(HAS_GPIO1)  || defined(HAS_GPIO2)  || defined(HAS_GPIO3)  || \
    defined(HAS_GPIO4)  || defined(HAS_GPIO5)  || defined(HAS_GPIO6)  || defined(HAS_GPIO7)  || \
    defined(HAS_GPIO8)  || defined(HAS_GPIO9)  || defined(HAS_GPIO10) || defined(HAS_GPIO11) || \
    defined(HAS_GPIO12) || defined(HAS_GPIO13) || defined(HAS_GPIO14) || defined(HAS_GPIO15)
#define HAS_GPIO
#endif


#ifdef _DEBUG_GPIO
#define DEBUG_GPIO(port, f, ...)    DEBUG("[%-14s] " f, (port)->id, ##__VA_ARGS__)
#else
#define DEBUG_GPIO(...)             {}
#endif


#ifdef HAS_GPIO0
extern port_t *                     gpio0;
#endif
#ifdef HAS_GPIO1
extern port_t *                     gpio1;
#endif
#ifdef HAS_GPIO2
extern port_t *                     gpio2;
#endif
#ifdef HAS_GPIO3
extern port_t *                     gpio3;
#endif
#ifdef HAS_GPIO4
extern port_t *                     gpio4;
#endif
#ifdef HAS_GPIO5
extern port_t *                     gpio5;
#endif
#ifdef HAS_GPIO6
extern port_t *                     gpio6;
#endif
#ifdef HAS_GPIO7
extern port_t *                     gpio7;
#endif
#ifdef HAS_GPIO8
extern port_t *                     gpio8;
#endif
#ifdef HAS_GPIO9
extern port_t *                     gpio9;
#endif
#ifdef HAS_GPIO10
extern port_t *                     gpio10;
#endif
#ifdef HAS_GPIO11
extern port_t *                     gpio11;
#endif
#ifdef HAS_GPIO12
extern port_t *                     gpio12;
#endif
#ifdef HAS_GPIO13
extern port_t *                     gpio13;
#endif
#ifdef HAS_GPIO14
extern port_t *                     gpio14;
#endif
#ifdef HAS_GPIO15
extern port_t *                     gpio15;
#endif


ICACHE_FLASH_ATTR void              gpio_init_ports(void);


#endif /* _PORTS_GPIO_H */
