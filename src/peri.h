
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

#include "ports.h"


#ifdef _DEBUG_PERI
#define DEBUG_PERI(fmt, ...)        DEBUG("[peripherals   ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_PERI(...)             {}
#endif

#define PERI_MAX_BYTE_PARAMS        8
#define PERI_MAX_LONG_PARAMS        4
#define PERI_MAX_RAW_PARAMS         24
#define PERI_MAX_NUM                16
#define PERI_MAX_PORTS              16
#define PERI_MAX_CLASS_ID           1   /* Needs to be adjusted when adding new peripheral classes */

#define PERI_FLAG(peri, no)         (!!((peri)->flags & (1 << (no))))
#define PERI_BYTE_PARAM(peri, no)   (peri)->byte_params[no]
#define PERI_LONG_PARAM(peri, no)   (peri)->long_params[no]


typedef struct {

    uint16                          class_id;
    uint16                          flags;

#pragma pack(push, 1)

    union {
        struct {
            int8                    byte_params[PERI_MAX_BYTE_PARAMS];
            int32                   long_params[PERI_MAX_LONG_PARAMS];
        };
        uint8                       raw_params[PERI_MAX_RAW_PARAMS];
    };

#pragma pack(pop)

} peri_t;

typedef void (*peri_make_ports_callback_t)(peri_t *peri, port_t **ports, uint8 *ports_len);

typedef struct {

    peri_make_ports_callback_t      make_ports;

} peri_class_t;


ICACHE_FLASH_ATTR void              peri_init(uint8 *config_data);
ICACHE_FLASH_ATTR void              peri_save(uint8 *config_data);

ICACHE_FLASH_ATTR void              peri_reset(void);
ICACHE_FLASH_ATTR void              peri_register(peri_t *peri, char *port_ids[], uint8 port_ids_len);

#endif /* _PERI_H */

