
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

#ifndef _ESPGOODIES_FLASHCFG_H
#define _ESPGOODIES_FLASHCFG_H


#ifdef _DEBUG_FLASHCFG
#define DEBUG_FLASHCFG(fmt, ...) DEBUG("[flashcfg      ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_FLASHCFG(...)      {}
#endif

#define FLASH_CONFIG_SLOT_DEFAULT 0 /* Slot for regular configuration */
#define FLASH_CONFIG_SLOT_SYSTEM  1 /* Slot for system configuration */

#define FLASH_CONFIG_SIZE_DEFAULT 0x2000 /*  8k bytes */
#define FLASH_CONFIG_SIZE_SYSTEM  0x1000 /*  4k bytes */

#define FLASH_CONFIG_OFFS_DEFAULT 0x0000
#define FLASH_CONFIG_OFFS_SYSTEM  0x2000


ICACHE_FLASH_ATTR bool flashcfg_load(uint8 slot, uint8 *data);
ICACHE_FLASH_ATTR bool flashcfg_save(uint8 slot, uint8 *data);
ICACHE_FLASH_ATTR bool flashcfg_reset(uint8 slot);


#endif /* _ESPGOODIES_FLASHCFG_H */
