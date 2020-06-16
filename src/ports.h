
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
#define DEBUG_PORT(port, fmt, ...)          DEBUG("[%-14s] " fmt, (port)->id, ##__VA_ARGS__)
#else
#define DEBUG_PORT(...)                     {}
#endif

#define PORT_MAX_ID_LEN                     64
#define PORT_MAX_DISP_NAME_LEN              64
#define PORT_MAX_UNIT_LEN                   16

#define PORT_MAX_SAMP_INT                   86400000    /* Milliseconds */
#define PORT_DEF_SAMP_INT                   0           /* Milliseconds */

#define PORT_DEF_HEART_BEAT_INT             0           /* Milliseconds */

#define PORT_TYPE_BOOLEAN                   'B'
#define PORT_TYPE_NUMBER                    'N'

#define ATTR_TYPE_BOOLEAN                   'B'
#define ATTR_TYPE_NUMBER                    'N'
#define ATTR_TYPE_STRING                    'S'

#define ATTRDEF_GS_TYPE_CUSTOM              0
#define ATTRDEF_GS_TYPE_EXTRA_DATA_1BU      1
#define ATTRDEF_GS_TYPE_EXTRA_DATA_1BS      2
#define ATTRDEF_GS_TYPE_EXTRA_DATA_2BU      3
#define ATTRDEF_GS_TYPE_EXTRA_DATA_2BS      4
#define ATTRDEF_GS_TYPE_EXTRA_DATA_4BS      5
#define ATTRDEF_GS_TYPE_EXTRA_DATA_BOOL     6
#define ATTRDEF_GS_TYPE_EXTRA_DATA_DOUBLE   7
#define ATTRDEF_GS_TYPE_FLAG                8

#define ATTRDEF_CACHE_EXTRA_INFO_FIELD(e, f) (offsetof(e, f) + 1 /* +1 is a common offset */)

#define PORT_PERSISTED_EXTRA_DATA_LEN       16

#define PORT_FLAG_ENABLED                   0x00000001
#define PORT_FLAG_OUTPUT                    0x00000002
#define PORT_FLAG_PULL_UP                   0x00000004
#define PORT_FLAG_PERSISTED                 0x00000008
#define PORT_FLAG_INTERNAL                  0x00000010
#define PORT_FLAG_SET                       0x00000020
#define PORT_FLAG_PULL_DOWN                 0x00000040  /* 0x00000080 - 0x00000800: reserved */

#define PORT_FLAG_VIRTUAL_INTEGER           0x00002000  /* 0x00001000: reserved */
#define PORT_FLAG_VIRTUAL_TYPE              0x00004000  /* 0x00008000: reserved */
#define PORT_FLAG_VIRTUAL_ACTIVE            0x00010000

#define PORT_FLAG_CUSTOM0                   0x01000000
#define PORT_FLAG_CUSTOM1                   0x02000000
#define PORT_FLAG_CUSTOM2                   0x04000000
#define PORT_FLAG_CUSTOM3                   0x08000000
#define PORT_FLAG_CUSTOM4                   0x10000000
#define PORT_FLAG_CUSTOM5                   0x20000000
#define PORT_FLAG_CUSTOM6                   0x40000000
#define PORT_FLAG_CUSTOM7                   0x80000000

#define PORT_FLAG_BIT_CUSTOM0               24
#define PORT_FLAG_BIT_CUSTOM1               25
#define PORT_FLAG_BIT_CUSTOM2               26
#define PORT_FLAG_BIT_CUSTOM3               27
#define PORT_FLAG_BIT_CUSTOM4               28
#define PORT_FLAG_BIT_CUSTOM5               29
#define PORT_FLAG_BIT_CUSTOM6               30
#define PORT_FLAG_BIT_CUSTOM7               31

#define PORT_SLOT_AUTO                      -1 // TODO remove these
#define PORT_SLOT_EXTRA0                    18
#define PORT_SLOT_EXTRA1                    19
#define PORT_SLOT_EXTRA2                    20
#define PORT_SLOT_EXTRA3                    21
#define PORT_SLOT_EXTRA4                    22
#define PORT_SLOT_EXTRA5                    23

#define PORT_SLOT_VIRTUAL0                  24

#define IS_ENABLED(port)                    ((port)->flags & PORT_FLAG_ENABLED)
#define IS_OUTPUT(port)                     ((port)->flags & PORT_FLAG_OUTPUT)
#define IS_PULL_UP(port)                    ((port)->flags & PORT_FLAG_PULL_UP)
#define IS_PULL_DOWN(port)                  ((port)->flags & PORT_FLAG_PULL_DOWN)
#define IS_PERSISTED(port)                  ((port)->flags & PORT_FLAG_PERSISTED)
#define IS_INTERNAL(port)                   ((port)->flags & PORT_FLAG_INTERNAL)
#define IS_SET(port)                        ((port)->flags & PORT_FLAG_SET)
#define IS_VIRTUAL(port)                    ((port)->flags & PORT_FLAG_VIRTUAL_ACTIVE)

#define CONFIG_OFFS_PORT_ID                 0x00    /*   4 bytes */
#define CONFIG_OFFS_PORT_DISP_NAME          0x04    /*   4 bytes */
#define CONFIG_OFFS_PORT_UNIT               0x08    /*   4 bytes */
#define CONFIG_OFFS_PORT_MIN                0x0C    /*   8 bytes */
#define CONFIG_OFFS_PORT_MAX                0x14    /*   8 bytes */
#define CONFIG_OFFS_PORT_STEP               0x1C    /*   8 bytes */
#define CONFIG_OFFS_PORT_CHOICES            0x24    /*   4 bytes */
                                                    /*  40 bytes reserved: 0x28 - 0x4F */
#define CONFIG_OFFS_PORT_FLAGS              0x50    /*   4 bytes */
#define CONFIG_OFFS_PORT_VALUE              0x54    /*   8 bytes */
#define CONFIG_OFFS_PORT_FWIDTH             0x5C    /*   4 bytes */
#define CONFIG_OFFS_PORT_EXPR               0x60    /*   4 bytes */
#define CONFIG_OFFS_PORT_TRANS_W            0x64    /*   4 bytes */
#define CONFIG_OFFS_PORT_TRANS_R            0x68    /*   4 bytes */
#define CONFIG_OFFS_PORT_SAMP_INT           0x6C    /*   4 bytes */
#define CONFIG_OFFS_PORT_DATA               0x70    /*  16 bytes for custom data */

#define CHANGE_REASON_NATIVE                'N'
#define CHANGE_REASON_API                   'A'
#define CHANGE_REASON_SEQUENCE              'S'
#define CHANGE_REASON_EXPRESSION            'E'


typedef struct attrdef {

    char          * name;
    char          * display_name;
    char          * description;
    char          * unit;
    char            type;
    bool            modifiable;
    double          min;
    double          max;
    union {
        double      def;
        bool        def_bool;
        char      * def_str;
    };
    bool            integer;
    double          step;
    bool            reconnect;
    char         ** choices;

    void          * get;
    void          * set;
    uint8           gs_type;
    union {
        uint8       gs_extra_data_offs;
        uint8       gs_flag_bit;
    };
    uint16          extra_info_cache_offs;

} attrdef_t;

typedef struct port {

    int8            slot;               /* Slot number */
    double          value;              /* Current value */

    char            change_reason;      /* Last value change reason */
    uint64          change_dep_mask;    /* Port change dependency mask */
    uint32          mutual_excl_mask;   /* Mask for ports that cannot be simultaneously enabled */

    int             aux;                /* Flag used internally for dependency loops & more */
    int8            mapped;             /* Flag used for mapping (e.g. pwm channel) */
    uint8           extra_data[PORT_PERSISTED_EXTRA_DATA_LEN];
    void          * extra_info;         /* In-memory extra state */

    /* Sampling */
    uint32          sampling_interval;
    int64           last_sample_time;   /* In milliseconds, since boot */
    uint32          min_sampling_interval;
    uint32          max_sampling_interval;
    uint32          def_sampling_interval;

    /* Value constraints */
    double          min;
    double          max;
    bool            integer;
    double          step;
    char         ** choices;

    /* Expressions */
    expr_t        * expr;
    char          * sexpr;
    expr_t        * transform_write;
    char          * stransform_write;
    expr_t        * transform_read;
    char          * stransform_read;
    
    /* Sequence */
    os_timer_t      sequence_timer;
    int16           sequence_len;
    int16           sequence_pos;
    int             sequence_repeat;
    double        * sequence_values;
    int           * sequence_delays;

    /* Common attributes */
    char            id[PORT_MAX_ID_LEN + 1];
    char          * display_name;
    char            type;
    char          * unit;
    uint32          flags;
    
    /* Heart beat */
    int             heart_beat_interval;
    uint64          last_heart_beat_time;   /* In milliseconds, since boot */

    /* Callbacks */
    double       (* read_value)(struct port *port);
    bool         (* write_value)(struct port *port, double value);
    void         (* configure)(struct port *port);
    void         (* heart_beat)(struct port *port);

    /* Extra attribute definitions */
    attrdef_t    ** attrdefs;

} port_t;


typedef int         (* int_getter_t)(struct port *port, attrdef_t *attrdef);
typedef void        (* int_setter_t)(struct port *port, attrdef_t *attrdef, int value);

typedef char      * (* str_getter_t)(struct port *port, attrdef_t *attrdef);
typedef void        (* str_setter_t)(struct port *port, attrdef_t *attrdef, char *value);

typedef double      (* float_getter_t)(struct port *port, attrdef_t *attrdef);
typedef void        (* float_setter_t)(struct port *port, attrdef_t *attrdef, double value);


extern port_t    ** all_ports;
extern char       * all_gpio_choices[];
extern char       * all_gpio_none_choices[];
extern char       * esp8266_gpio_choices[];
extern char       * esp8266_gpio_none_choices[];


ICACHE_FLASH_ATTR void      ports_init(uint8 *config_data);
ICACHE_FLASH_ATTR void      ports_save(uint8 *config_data, uint32 *strings_offs);

ICACHE_FLASH_ATTR bool      ports_slot_busy(uint8 slot);
ICACHE_FLASH_ATTR int8      ports_next_slot(void);

ICACHE_FLASH_ATTR void      port_register(port_t *port);
ICACHE_FLASH_ATTR bool      port_unregister(port_t *port);
ICACHE_FLASH_ATTR void      port_cleanup(port_t *port);
ICACHE_FLASH_ATTR port_t  * port_find_by_id(char *id);
ICACHE_FLASH_ATTR void      port_rebuild_change_dep_mask(port_t *port);
ICACHE_FLASH_ATTR void      port_sequence_cancel(port_t *port);
ICACHE_FLASH_ATTR void      port_expr_remove(port_t *port);
ICACHE_FLASH_ATTR bool      port_set_value(port_t *port, double value, char reason);
ICACHE_FLASH_ATTR json_t  * port_get_json_value(port_t *port);
ICACHE_FLASH_ATTR void      port_enable(port_t *port);
ICACHE_FLASH_ATTR void      port_disable(port_t *port);
ICACHE_FLASH_ATTR void      port_configure(port_t *port);


#endif  /* _PORTS_H */
