
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

/*
 * Dallas Temperature sensors.
 * Inspired from https://github.com/milesburton/Arduino-Temperature-Control-Library/
 */

#ifndef _EXTRA_DALLASTEMP_H
#define _EXTRA_DALLASTEMP_H

#include <ets_sys.h>

#include "drivers/onewire.h"
#include "ports.h"


#ifdef _DEBUG_DALLASTEMP
#define DEBUG_DT(port, f, ...)              DEBUG("[%-14s] " f, (port)->id, ##__VA_ARGS__)
#else
#define DEBUG_DT(...)                       {}
#endif

ICACHE_FLASH_ATTR void                      dallastemp_init_ports(void);


#endif /* _EXTRA_DALLASTEMP_H */
