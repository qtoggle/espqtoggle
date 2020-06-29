
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

#ifndef _STRING_POOL_H
#define _STRING_POOL_H


#include "espgoodies/common.h"


ICACHE_FLASH_ATTR char *string_pool_read(char *pool, void *offs_save);
ICACHE_FLASH_ATTR char *string_pool_read_dup(char *pool, void *offs_save);
ICACHE_FLASH_ATTR bool  string_pool_write(char *pool, uint32 *pool_offs, char *value, void *offs_save);


#endif /* _STRING_POOL_H */
