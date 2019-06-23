
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

#ifndef _ESPGOODIES_UTILS_H
#define _ESPGOODIES_UTILS_H


#include <os_type.h>
#include <ctype.h>


#define IS_HEX(c)               (isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
#define CONN_EQUAL(c1, c2)      (!memcmp(c1->proto.tcp->remote_ip, c2->proto.tcp->remote_ip, 4) && \
                                 c1->proto.tcp->remote_port == c2->proto.tcp->remote_port)
#define MIN(a, b)               (a) > (b) ? (b) : (a)
#define MAX(a, b)               (a) > (b) ? (a) : (b)


ICACHE_FLASH_ATTR void          append_max_len(char *s, char c, int max_len);
ICACHE_FLASH_ATTR int           realloc_chunks(char **p, int current_size, int req_size);
ICACHE_FLASH_ATTR double        strtod(const char *s, char **endptr);
ICACHE_FLASH_ATTR char        * dtostr(double d, int8 decimals);
ICACHE_FLASH_ATTR double        decent_round(double d);

ICACHE_FLASH_ATTR int           gpio_get_mux(int gpio_no);
ICACHE_FLASH_ATTR int           gpio_get_func(int gpio_no);
ICACHE_FLASH_ATTR void          gpio_select_func(int gpio_no);
ICACHE_FLASH_ATTR void          gpio_set_pullup(int gpio_no, bool enabled);


#endif /* _ESPGOODIES_UTILS_H */
