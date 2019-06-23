
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


ICACHE_FLASH_ATTR void              gpio_init_ports();


#endif /* _PORTS_GPIO_H */
