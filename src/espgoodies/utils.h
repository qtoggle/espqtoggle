
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


#define IS_HEX(c)               (isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
#define CONN_EQUAL(c1, c2)      (!memcmp(c1->proto.tcp->remote_ip, c2->proto.tcp->remote_ip, 4) && \
                                 c1->proto.tcp->remote_port == c2->proto.tcp->remote_port)
#define MIN(a, b)               (a) > (b) ? (b) : (a)
#define MAX(a, b)               (a) > (b) ? (a) : (b)

#define FMT_UINT64_VAL(value)   ((unsigned long) (value >> 32)), ((unsigned long) value)
#define FMT_UINT64_HEX          "%08lX%08lX"

#define htons(x)                (((x)<< 8 & 0xFF00) | ((x)>> 8 & 0x00FF))
#define ntohs(x)                htons(x)

#define htonl(x)                (((x)<<24 & 0xFF000000UL) | \
                                 ((x)<< 8 & 0x00FF0000UL) | \
                                 ((x)>> 8 & 0x0000FF00UL) | \
                                 ((x)>>24 & 0x000000FFUL))

#define ntohl(x)                htonl(x)


typedef void (* call_later_callback_t)(void *arg);


ICACHE_FLASH_ATTR void          append_max_len(char *s, char c, int max_len);
ICACHE_FLASH_ATTR int           realloc_chunks(char **p, int current_size, int req_size);
ICACHE_FLASH_ATTR double        strtod(const char *s, char **endptr);
ICACHE_FLASH_ATTR char        * dtostr(double d, int8 decimals);
ICACHE_FLASH_ATTR double        decent_round(double d);
ICACHE_FLASH_ATTR double        round_to(double d, uint8 decimals);
ICACHE_FLASH_ATTR int           compare_double(const void *a, const void *b);
ICACHE_FLASH_ATTR bool          call_later(call_later_callback_t callback, void *arg, uint32 delay_ms);


#endif /* _ESPGOODIES_UTILS_H */
