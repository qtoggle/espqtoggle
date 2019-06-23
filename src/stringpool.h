
/*
 * Copyright (c) Calin Crisan
 * This file is part of espqToggle.
 *
 * espqToggle is free software: you can redistribute it and/or modify
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

#ifndef _STRING_POOL_H
#define _STRING_POOL_H


#include "espgoodies/common.h"


ICACHE_FLASH_ATTR char            * string_pool_read(char *pool, void *offs_save);
ICACHE_FLASH_ATTR char            * string_pool_read_dup(char *pool, void *offs_save);
ICACHE_FLASH_ATTR bool              string_pool_write(char *pool, uint32 *pool_offs, char *value, void *offs_save);


#endif /* _STRING_POOL_H */
