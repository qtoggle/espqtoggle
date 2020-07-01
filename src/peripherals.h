
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
#define DEBUG_PERIPHERALS(fmt, ...)   DEBUG("[peripherals   ] " fmt, ##__VA_ARGS__)
#define DEBUG_PERIPHERAL(p, fmt, ...) DEBUG("[peripheral%02d  ] " fmt, (p)->index, ##__VA_ARGS__)
#else
#define DEBUG_PERIPHERALS(...)        {}
#endif

#define PERIPHERAL_CONFIG_OFFS_TYPE_ID       0x00 /*  2 bytes */
#define PERIPHERAL_CONFIG_OFFS_FLAGS         0x02 /*  2 bytes */
                                                  /* 0x04 - 0x08: reserved */
#define PERIPHERAL_CONFIG_OFFS_PARAMS        0x08 /* 56 bytes */

#define PERIPHERAL_CONFIG_OFFS_INT8_PARAMS   0x00 /* relative to start of params */
#define PERIPHERAL_CONFIG_OFFS_INT16_PARAMS  0x08
#define PERIPHERAL_CONFIG_OFFS_INT32_PARAMS  0x10
#define PERIPHERAL_CONFIG_OFFS_INT64_PARAMS  0x20
#define PERIPHERAL_CONFIG_OFFS_DOUBLE_PARAMS 0x20

#define PERIPHERAL_PARAMS_SIZE       56
#define PERIPHERAL_MAX_INT8_PARAMS   56 /* 8 non-overlapping bytes */
#define PERIPHERAL_MAX_INT16_PARAMS  24 /* 4 non-overlapping shorts */
#define PERIPHERAL_MAX_INT32_PARAMS  10 /* 4 non-overlapping longs */
#define PERIPHERAL_MAX_INT64_PARAMS  3  /* 3 non-overlapping long-longs or doubles */
#define PERIPHERAL_MAX_DOUBLE_PARAMS 3
#define PERIPHERAL_MAX_NUM           16 /* Max number of registered peripherals */
#define PERIPHERAL_MAX_PORTS         16 /* Max number of ports/peripheral */
#define PERIPHERAL_MAX_TYPE_ID       3  /* Needs to be increased when adding a new peripheral type */

#define PERIPHERAL_GET_FLAG(p, no)     (!!((p)->flags & (1 << (no))))
#define PERIPHERAL_SET_FLAG(p, no, v)  {if (v) (p)->flags |= (1 << (no)); else (p)->flags &= ~(1 << (no));}
#define PERIPHERAL_PARAM_UINT8(p, no)  (p)->params[no]
#define PERIPHERAL_PARAM_SINT8(p, no)  ((int8 *) (p)->params)[no]
#define PERIPHERAL_PARAM_UINT16(p, no) ((uint16 *) (p)->params)[no + 8]
#define PERIPHERAL_PARAM_SINT16(p, no) ((int16 *) (p)->params)[no + 8]
#define PERIPHERAL_PARAM_UINT32(p, no) ((uint32 *) (p)->params)[no + 16]
#define PERIPHERAL_PARAM_SINT32(p, no) ((int32 *) (p)->params)[no + 16]
#define PERIPHERAL_PARAM_UINT64(p, no) ((uint64 *) (p)->params)[no + 32]
#define PERIPHERAL_PARAM_SINT64(p, no) ((uint64*) (p)->params)[no + 32]
#define PERIPHERAL_PARAM_DOUBLE(p, no) ((double *) (p)->params)[no + 32]


typedef struct peripheral {

    uint8   index;
    uint16  type_id;
    uint16  flags;
    uint8  *params;

    void   *user_data; /* In-memory user state */

} peripheral_t;

typedef struct {

    void (*init)(peripheral_t *peripheral);
    void (*cleanup)(peripheral_t *peripheral);
    void (*make_ports)(peripheral_t *peripheral, port_t **ports, uint8 *ports_len);

} peripheral_type_t;


extern peripheral_t **all_peripherals;
extern uint8          all_peripherals_count;


ICACHE_FLASH_ATTR void peripherals_init(uint8 *config_data);
ICACHE_FLASH_ATTR void peripherals_save(uint8 *config_data, uint32 *strings_offs);

ICACHE_FLASH_ATTR void peripheral_init(peripheral_t *peripheral, char *port_ids[], uint8 port_ids_len);
ICACHE_FLASH_ATTR void peripheral_cleanup(peripheral_t *peripheral);
ICACHE_FLASH_ATTR void peripheral_register(peripheral_t *peripheral);
ICACHE_FLASH_ATTR void peripheral_unregister(peripheral_t *peripheral);


#endif /* _PERIPHERALS_H */
