
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

#ifndef _ESPGOODIES_VERSION_H
#define _ESPGOODIES_VERSION_H


#include <os_type.h>


#define VERSION_TYPE_ALPHA   1
#define VERSION_TYPE_BETA    2
#define VERSION_TYPE_RC      3
#define VERSION_TYPE_UNKNOWN 5
#define VERSION_TYPE_STABLE  6
#define VERSION_TYPE_GIT     7


typedef struct {
    uint8 major;
    uint8 minor;
    uint8 patch;
    uint8 label;
    uint8 type;
} version_t;


void   ICACHE_FLASH_ATTR  version_parse(char *version_str, version_t *version);
char   ICACHE_FLASH_ATTR *version_dump(version_t *version);
void   ICACHE_FLASH_ATTR  version_from_int(version_t *version, uint32 version_int);
uint32 ICACHE_FLASH_ATTR  version_to_int(version_t *version);
char   ICACHE_FLASH_ATTR *version_type_str(uint8 type);
int    ICACHE_FLASH_ATTR  version_cmp(version_t *version1, version_t *version2);


#endif /* _ESPGOODIES_VERSION_H */
