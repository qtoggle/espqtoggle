
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

#ifndef _ESPGOODIES_COMMON_H
#define _ESPGOODIES_COMMON_H


#include <math.h>
#include <osapi.h>
#include <os_type.h>

#include "espmissingincludes.h"


/* we need to alter the ICACHE_RODATA_ATTR definition to add 4-byte alignment specifier */
#ifdef ICACHE_RODATA_ATTR
#undef ICACHE_RODATA_ATTR
#endif
#define ICACHE_RODATA_ATTR      __attribute__((aligned(4))) __attribute__((section(".irom.text")))

/* we need to redefine these string functions here because the built-in ones fail at at linkage */
#define strtok                  my_strtok
#define strdup                  my_strdup
#define strndup                 my_strndup

#define malloc                  os_malloc
#define realloc                 os_realloc
#define free                    os_free
#define zalloc                  os_zalloc

#define printf                  os_printf

#ifdef _DEBUG_IP
#undef os_printf
#define os_printf               udp_printf
#endif

#if defined(_DEBUG) && !defined(_GDB)
#define DEBUG(fmt, ...)         os_printf("DEBUG: " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG(...)              {}
#endif

#define UNDEFINED               NAN
#define IS_UNDEFINED(x)         isnan(x)


ICACHE_FLASH_ATTR char        * my_strtok(char *s, char *d);
ICACHE_FLASH_ATTR char        * my_strdup(const char *s);
ICACHE_FLASH_ATTR char        * my_strndup(const char *s, int n);

#ifdef _DEBUG_IP
ICACHE_FLASH_ATTR int           udp_printf(const char *format, ...);
#endif


#endif /* _ESPGOODIES_COMMON_H */
