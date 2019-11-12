
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

#ifndef _ESPGOODIES_UTILS_H
#define _ESPGOODIES_UTILS_H


#include <os_type.h>
#include <ctype.h>


#define IS_HEX(c)                   (isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
#define CONN_EQUAL(c1, c2)          (!memcmp(c1->proto.tcp->remote_ip, c2->proto.tcp->remote_ip, 4) && \
                                     c1->proto.tcp->remote_port == c2->proto.tcp->remote_port)
#define MIN(a, b)                   (a) > (b) ? (b) : (a)
#define MAX(a, b)                   (a) > (b) ? (a) : (b)

#define htons(x)                    (((x)<< 8 & 0xFF00) | ((x)>> 8 & 0x00FF))
#define ntohs(x)                    htons(x)

#define htonl(x)                    (((x)<<24 & 0xFF000000UL) | \
                                     ((x)<< 8 & 0x00FF0000UL) | \
                                     ((x)>> 8 & 0x0000FF00UL) | \
                                     ((x)>>24 & 0x000000FFUL))

#define ntohl(x)                    htonl(x)


#ifdef _DEBUG_GPIO_UTILS
#define DEBUG_GPIO_UTILS(fmt, ...)  DEBUG("[gpio          ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_GPIO_UTILS(...)             {}
#endif


ICACHE_FLASH_ATTR void          append_max_len(char *s, char c, int max_len);
ICACHE_FLASH_ATTR int           realloc_chunks(char **p, int current_size, int req_size);
ICACHE_FLASH_ATTR double        strtod(const char *s, char **endptr);
ICACHE_FLASH_ATTR char        * dtostr(double d, int8 decimals);
ICACHE_FLASH_ATTR double        decent_round(double d);

ICACHE_FLASH_ATTR int           gpio_get_mux(int gpio_no);
ICACHE_FLASH_ATTR int           gpio_get_func(int gpio_no);
ICACHE_FLASH_ATTR void          gpio_select_func(int gpio_no);
ICACHE_FLASH_ATTR void          gpio_set_pull(int gpio_no, bool pull);

ICACHE_FLASH_ATTR void          gpio_configure_input(int gpio_no, bool pull);
ICACHE_FLASH_ATTR void          gpio_configure_output(int gpio_no, bool initial);

ICACHE_FLASH_ATTR void          gpio_write_value(int gpio_no, bool value);
ICACHE_FLASH_ATTR bool          gpio_read_value(int gpio_no);


#endif /* _ESPGOODIES_UTILS_H */
