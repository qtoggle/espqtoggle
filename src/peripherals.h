
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

#ifndef _PERIPHERALS_H
#define _PERIPHERALS_H


#include <c_types.h>

#include "espgoodies/json.h"

#include "ports.h"


#ifdef _DEBUG_PERIPHERALS
#define DEBUG_PERIPHERALS(fmt, ...)       DEBUG("[peripherals   ] " fmt, ##__VA_ARGS__)
#define DEBUG_PERIPHERAL(p, fmt, ...)     DEBUG("[peripheral%d  ] " fmt, (p)->index, ##__VA_ARGS__)
#else
#define DEBUG_PERIPHERALS(...)            {}
#endif

#define PERIPHERAL_MAX_BYTE_PARAMS        8
#define PERIPHERAL_MAX_LONG_PARAMS        4
#define PERIPHERAL_MAX_RAW_PARAMS         24
#define PERIPHERAL_MAX_NUM                16
#define PERIPHERAL_MAX_PORTS              16
#define PERIPHERAL_MAX_TYPE_ID           1   /* Needs to be adjusted when adding new peripheral types */

#define PERIPHERAL_FLAG(p, no)         (!!((p)->flags & (1 << (no))))
#define PERIPHERAL_BYTE_PARAM(p, no)   (p)->byte_params[no]
#define PERIPHERAL_LONG_PARAM(p, no)   (p)->long_params[no]


typedef struct {

    uint8                           index;
    uint16                          type_id;
    uint16                          flags;

#pragma pack(push, 1)

    union {
        struct {
            int8                    byte_params[PERIPHERAL_MAX_BYTE_PARAMS];
            int32                   long_params[PERIPHERAL_MAX_LONG_PARAMS];
        };
        uint8                       raw_params[PERIPHERAL_MAX_RAW_PARAMS];
    };

#pragma pack(pop)

} peripheral_t;

typedef void (*peripheral_make_ports_callback_t)(peripheral_t *peripheral, port_t **ports, uint8 *ports_len);

typedef struct {

    peripheral_make_ports_callback_t      make_ports;

} peripheral_type_t;


ICACHE_FLASH_ATTR void              peripherals_init(uint8 *config_data);
ICACHE_FLASH_ATTR void              peripherals_save(uint8 *config_data, uint32 *strings_offs);
ICACHE_FLASH_ATTR void              peripherals_clear(void);

ICACHE_FLASH_ATTR void              peripheral_make_ports(peripheral_t *peripheral, char *port_ids[],
                                                          uint8 port_ids_len);
ICACHE_FLASH_ATTR void              peripheral_register(peripheral_t *peripheral);
ICACHE_FLASH_ATTR void              peripheral_unregister(peripheral_t *peripheral);


#endif /* _PERIPHERALS_H */
