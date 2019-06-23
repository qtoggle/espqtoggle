
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


#include "stringpool.h"
#include "config.h"


char *string_pool_read(char *pool, void *offs_save) {
    uint32 offs;
    memcpy(&offs, offs_save, 4);

    if (offs == 0 || offs >= 65536) {  /* some extra checking, just in case */
        return NULL;
    }

    return pool + offs;
}

char *string_pool_read_dup(char *pool, void *offs_save) {
    char *s = string_pool_read(pool, offs_save);
    if (s) {
        return strdup(s);
    }
    else {
        return NULL;
    }
}

bool string_pool_write(char *pool, uint32 *pool_offs, char *value, void *offs_save) {
    if (!value) {
        memset(offs_save, 0, 4);
        return TRUE;
    }

    int len = strlen(value) + 1;
    if (len > CONFIG_SIZE_STR - *pool_offs) {
        return FALSE;
    }

    memcpy(pool + *pool_offs, value, len);
    memcpy(offs_save, pool_offs, 4);
    *pool_offs += len;

    return TRUE;
}
