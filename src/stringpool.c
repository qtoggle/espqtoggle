
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

#include "common.h"
#include "config.h"
#include "stringpool.h"


char *string_pool_read(char *pool, void *offs_save) {
    uint32 offs;
    memcpy(&offs, offs_save, 4);

    if (offs == 0 || offs >= 65536) {  /* Some extra checking, just in case */
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
    if (len > CONFIG_STR_SIZE - *pool_offs) {
        return FALSE;
    }

    memcpy(pool + *pool_offs, value, len);
    memcpy(offs_save, pool_offs, 4);
    *pool_offs += len;

    return TRUE;
}
