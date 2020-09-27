
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

#ifndef _PORTS_H
#define _PORTS_H


#include <sys/cdefs.h>
#include <os_type.h>

#include "espgoodies/common.h"
#include "espgoodies/json.h"

#include "expr.h"


#ifdef _DEBUG_PORTS
#define DEBUG_PORT(port, fmt, ...) DEBUG("[%-14s] " fmt, (port)->id, ##__VA_ARGS__)
#else
#define DEBUG_PORT(...)            {}
#endif

#define ATTR_TYPE_BOOLEAN 'B'
#define ATTR_TYPE_NUMBER  'N'
#define ATTR_TYPE_STRING  'S'

#define ATTRDEF_FLAG_MODIFIABLE 0x00000001
#define ATTRDEF_FLAG_INTEGER    0x00000002
#define ATTRDEF_FLAG_RECONNECT  0x00000004

#define IS_ATTRDEF_MODIFIABLE(a) !!((a)->flags & ATTRDEF_FLAG_MODIFIABLE)
#define IS_ATTRDEF_INTEGER(a)    !!((a)->flags & ATTRDEF_FLAG_INTEGER)
#define IS_ATTRDEF_RECONNECT(a)  !!((a)->flags & ATTRDEF_FLAG_RECONNECT)

#define ATTRDEF_STORAGE_CUSTOM       0
#define ATTRDEF_STORAGE_PARAM_UINT8  1
#define ATTRDEF_STORAGE_PARAM_SINT8  2
#define ATTRDEF_STORAGE_PARAM_UINT16 3
#define ATTRDEF_STORAGE_PARAM_SINT16 4
#define ATTRDEF_STORAGE_PARAM_UINT32 5
#define ATTRDEF_STORAGE_PARAM_SINT32 6
#define ATTRDEF_STORAGE_PARAM_SINT64 7 /* UINT64 can't be implemented since our int_getter_t returns int64 */
#define ATTRDEF_STORAGE_PARAM_DOUBLE 8
#define ATTRDEF_STORAGE_FLAG         9

#define ATTRDEF_CACHE_USER_DATA_FIELD(e, f) (offsetof(e, f) + 1 /* +1 is a common offset */)

#define PORT_MAX_ID_LEN        64
#define PORT_MAX_DISP_NAME_LEN 64
#define PORT_MAX_UNIT_LEN      16

#define PORT_DEF_HEART_BEAT_INT 0  /* Milliseconds */

#define PORT_TYPE_BOOLEAN 'B'
#define PORT_TYPE_NUMBER  'N'

#define PORT_FLAG_ENABLED         0x00000001
#define PORT_FLAG_WRITABLE        0x00000002
#define PORT_FLAG_SET             0x00000004
#define PORT_FLAG_PERSISTED       0x00000008
#define PORT_FLAG_INTERNAL        0x00000010 /* 0x00000020 - 0x00000800: reserved */

#define PORT_FLAG_VIRTUAL_INTEGER 0x00001000
#define PORT_FLAG_VIRTUAL_TYPE    0x00002000
#define PORT_FLAG_VIRTUAL_ACTIVE  0x00004000

#define PORT_FLAG_CUSTOM0         0x01000000
#define PORT_FLAG_CUSTOM1         0x02000000
#define PORT_FLAG_CUSTOM2         0x04000000
#define PORT_FLAG_CUSTOM3         0x08000000
#define PORT_FLAG_CUSTOM4         0x10000000
#define PORT_FLAG_CUSTOM5         0x20000000
#define PORT_FLAG_CUSTOM6         0x40000000
#define PORT_FLAG_CUSTOM7         0x80000000

#define PORT_FLAG_BIT_CUSTOM0     24
#define PORT_FLAG_BIT_CUSTOM1     25
#define PORT_FLAG_BIT_CUSTOM2     26
#define PORT_FLAG_BIT_CUSTOM3     27
#define PORT_FLAG_BIT_CUSTOM4     28
#define PORT_FLAG_BIT_CUSTOM5     29
#define PORT_FLAG_BIT_CUSTOM6     30
#define PORT_FLAG_BIT_CUSTOM7     31

#define PORT_SLOT_EXTRA0   18
#define PORT_SLOT_VIRTUAL0 24

#define IS_PORT_ENABLED(port)   !!((port)->flags & PORT_FLAG_ENABLED)
#define IS_PORT_WRITABLE(port)  !!((port)->flags & PORT_FLAG_WRITABLE)
#define IS_PORT_SET(port)       !!((port)->flags & PORT_FLAG_SET)
#define IS_PORT_PERSISTED(port) !!((port)->flags & PORT_FLAG_PERSISTED)
#define IS_PORT_INTERNAL(port)  !!((port)->flags & PORT_FLAG_INTERNAL)
#define IS_PORT_VIRTUAL(port)   !!((port)->flags & PORT_FLAG_VIRTUAL_ACTIVE)

#define PORT_CONFIG_OFFS_ID        0x00 /*  4 bytes */
#define PORT_CONFIG_OFFS_DISP_NAME 0x04 /*  4 bytes */
#define PORT_CONFIG_OFFS_UNIT      0x08 /*  4 bytes */
#define PORT_CONFIG_OFFS_MIN       0x0C /*  8 bytes */
#define PORT_CONFIG_OFFS_MAX       0x14 /*  8 bytes */
#define PORT_CONFIG_OFFS_STEP      0x1C /*  8 bytes */
#define PORT_CONFIG_OFFS_CHOICES   0x24 /*  4 bytes */
#define PORT_CONFIG_OFFS_FLAGS     0x28 /*  4 bytes */
#define PORT_CONFIG_OFFS_VALUE     0x2C /*  8 bytes */
#define PORT_CONFIG_OFFS_EXPR      0x34 /*  4 bytes */
#define PORT_CONFIG_OFFS_TRANS_W   0x38 /*  4 bytes */
#define PORT_CONFIG_OFFS_TRANS_R   0x3C /*  4 bytes */
#define PORT_CONFIG_OFFS_SAMP_INT  0x40 /*  4 bytes */
                                        /* 0x44 - 0x60: reserved */

#define CHANGE_REASON_NATIVE     'N'
#define CHANGE_REASON_API        'A'
#define CHANGE_REASON_SEQUENCE   'S'
#define CHANGE_REASON_EXPRESSION 'E'


typedef struct attrdef {

    char        *name;
    char        *display_name;
    char        *description;
    char        *unit;
    char         type;
    uint32       flags;
    double       min;
    double       max;
    double       step;
    char       **choices;

    union {
        double   def;
        bool     def_bool;
        char    *def_str;
    };

    void        *get;
    void        *set;
    uint8        storage;
    union {
        uint8    storage_param_no;
        uint8    storage_flag_bit_no;
    };
    uint16       cache_user_data_field_offs;

} attrdef_t;

typedef struct port {

    struct peripheral *peripheral;

    int8               slot;                  /* Slot number */
    double             value;                 /* Current value */

    char               change_reason;         /* Last value-change reason */
    uint64             change_dep_mask;       /* Port change dependency mask */

    int                aux;                   /* Member used internally for dependency loops & more */
    void              *user_data;             /* Generic pointer to user data */

    /* Sampling */
    uint32             sampling_interval;
    int64              last_sample_time;      /* In milliseconds, since boot */
    uint32             min_sampling_interval;
    uint32             max_sampling_interval;
    uint32             def_sampling_interval;

    /* Value constraints */
    double             min;
    double             max;
    bool               integer;
    double             step;
    char             **choices;

    /* Expressions */
    expr_t            *expr;
    char              *sexpr;
    expr_t            *transform_write;
    char              *stransform_write;
    expr_t            *transform_read;
    char              *stransform_read;

    /* Sequence */
    os_timer_t         sequence_timer;
    int16              sequence_len;
    int16              sequence_pos;
    int                sequence_repeat;
    double            *sequence_values;
    int               *sequence_delays;

    /* Common attributes */
    char              *id;
    char              *display_name;
    char               type;
    char              *unit;
    uint32             flags;

    /* Heart beat */
    int                heart_beat_interval;
    uint64             last_heart_beat_time;  /* In milliseconds, since boot */

    /* Callbacks */
    double             (*read_value)(struct port *port);
    bool               (*write_value)(struct port *port, double value);
    void               (*configure)(struct port *port, bool enabled);
    void               (*heart_beat)(struct port *port);

    /* Extra attribute definitions */
    attrdef_t        **attrdefs;

} port_t;


typedef int64   (*int_getter_t)(struct port *port, attrdef_t *attrdef);
typedef void    (*int_setter_t)(struct port *port, attrdef_t *attrdef, int64 value);

typedef char   *(*str_getter_t)(struct port *port, attrdef_t *attrdef);
typedef void    (*str_setter_t)(struct port *port, attrdef_t *attrdef, char *value);

typedef double  (*float_getter_t)(struct port *port, attrdef_t *attrdef);
typedef void    (*float_setter_t)(struct port *port, attrdef_t *attrdef, double value);


extern port_t **all_ports;
extern int      all_ports_count;


void   ICACHE_FLASH_ATTR  ports_init(uint8 *config_data);
void   ICACHE_FLASH_ATTR  ports_save(uint8 *config_data, uint32 *strings_offs);

bool   ICACHE_FLASH_ATTR  ports_slot_busy(uint8 slot);
int8   ICACHE_FLASH_ATTR  ports_next_slot(void);
void   ICACHE_FLASH_ATTR  ports_rebuild_change_dep_mask(void);

port_t ICACHE_FLASH_ATTR *port_create(void);
void   ICACHE_FLASH_ATTR  port_register(port_t *port);
bool   ICACHE_FLASH_ATTR  port_unregister(port_t *port);
void   ICACHE_FLASH_ATTR  port_cleanup(port_t *port);
port_t ICACHE_FLASH_ATTR *port_find_by_id(char *id);
port_t ICACHE_FLASH_ATTR *port_find_by_slot(uint8 slot);
void   ICACHE_FLASH_ATTR  port_rebuild_change_dep_mask(port_t *port);
void   ICACHE_FLASH_ATTR  port_sequence_cancel(port_t *port);
void   ICACHE_FLASH_ATTR  port_expr_remove(port_t *port);
bool   ICACHE_FLASH_ATTR  port_set_value(port_t *port, double value, char reason);
json_t ICACHE_FLASH_ATTR *port_get_json_value(port_t *port);
void   ICACHE_FLASH_ATTR  port_enable(port_t *port);
void   ICACHE_FLASH_ATTR  port_disable(port_t *port);
void   ICACHE_FLASH_ATTR  port_configure(port_t *port);


#endif  /* _PORTS_H */
