
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

#ifndef _ESPGOODIES_COMMON_H
#define _ESPGOODIES_COMMON_H


#include <math.h>
#include <osapi.h>
#include <os_type.h>


/* we need to alter the ICACHE_RODATA_ATTR definition to add 4-byte alignment specifier */
#ifdef ICACHE_RODATA_ATTR
#undef ICACHE_RODATA_ATTR
#endif
#define ICACHE_RODATA_ATTR      __attribute__((aligned(4))) __attribute__((section(".irom.text")))

/* we need to redefine these string functions here because the built-in ones fail at linkage */
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

#ifdef _DEBUG
#define DEBUG(fmt, ...)         os_printf("DEBUG: " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG(...)              {}
#endif


ICACHE_FLASH_ATTR char        * my_strtok(char *s, char *d);
ICACHE_FLASH_ATTR char        * my_strdup(const char *s);
ICACHE_FLASH_ATTR char        * my_strndup(const char *s, int n);

#ifdef _DEBUG_IP
ICACHE_FLASH_ATTR int           udp_printf(const char *format, ...);
#endif


#endif /* _ESPGOODIES_COMMON_H */
