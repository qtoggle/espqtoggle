
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

#ifndef _PERI_H
#define _PERI_H


#include <c_types.h>

#include "espgoodies/json.h"


#ifdef _DEBUG_PERI
#define DEBUG_PERI(fmt, ...)            DEBUG("[peripherals   ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_PERI(...)                 {}
#endif


struct peri;

typedef struct peri_class {

    char                          * name;
    void                         (* configure)(struct peri *peri, uint8* slots, json_t *params);

} peri_class_t;


typedef struct peri {

    peri_class_t                  * cls;
    uint8                         * slots;
    void                          * data;

} peri_t;


ICACHE_FLASH_ATTR peri_class_t    * peri_class_lookup(char *name);
ICACHE_FLASH_ATTR void              peri_register(peri_t *peri);
ICACHE_FLASH_ATTR void              peri_unregister_all(void);


#endif /* _PERI_H */
