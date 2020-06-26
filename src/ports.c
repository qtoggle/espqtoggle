
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

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <c_types.h>
#include <mem.h>
#include <pwm.h>

#include "espgoodies/common.h"
#include "espgoodies/flashcfg.h"
#include "espgoodies/utils.h"

#include "apiutils.h"
#include "common.h"
#include "config.h"
#include "core.h"
#include "events.h"
#include "peripherals.h"
#include "stringpool.h"
#include "virtual.h"
#include "ports.h"


port_t                           ** all_ports = NULL;
int                                 all_ports_count = 0;
static uint32                       used_slots = 0;


ICACHE_FLASH_ATTR static int64      attr_get_param_uint8(port_t *port, attrdef_t *attrdef);
ICACHE_FLASH_ATTR static void       attr_set_param_uint8(port_t *port, attrdef_t *attrdef, int64 value);

ICACHE_FLASH_ATTR static int64      attr_get_param_sint8(port_t *port, attrdef_t *attrdef);
ICACHE_FLASH_ATTR static void       attr_set_param_sint8(port_t *port, attrdef_t *attrdef, int64 value);

ICACHE_FLASH_ATTR static int64      attr_get_param_uint16(port_t *port, attrdef_t *attrdef);
ICACHE_FLASH_ATTR static void       attr_set_param_uint16(port_t *port, attrdef_t *attrdef, int64 value);

ICACHE_FLASH_ATTR static int64      attr_get_param_sint16(port_t *port, attrdef_t *attrdef);
ICACHE_FLASH_ATTR static void       attr_set_param_sint16(port_t *port, attrdef_t *attrdef, int64 value);

ICACHE_FLASH_ATTR static int64      attr_get_param_uint32(port_t *port, attrdef_t *attrdef);
ICACHE_FLASH_ATTR static void       attr_set_param_uint32(port_t *port, attrdef_t *attrdef, int64 value);

ICACHE_FLASH_ATTR static int64      attr_get_param_sint32(port_t *port, attrdef_t *attrdef);
ICACHE_FLASH_ATTR static void       attr_set_param_sint32(port_t *port, attrdef_t *attrdef, int64 value);

ICACHE_FLASH_ATTR static int64      attr_get_param_sint64(port_t *port, attrdef_t *attrdef);
ICACHE_FLASH_ATTR static void       attr_set_param_sint64(port_t *port, attrdef_t *attrdef, int64 value);

ICACHE_FLASH_ATTR static double     attr_get_param_double(port_t *port, attrdef_t *attrdef);
ICACHE_FLASH_ATTR static void       attr_set_param_double(port_t *port, attrdef_t *attrdef, double value);

ICACHE_FLASH_ATTR static int        attr_get_flag(port_t *port, attrdef_t *attrdef);
ICACHE_FLASH_ATTR static void       attr_set_flag(port_t *port, attrdef_t *attrdef, int value);

ICACHE_FLASH_ATTR static int        attr_get_param_num_choices(port_t *port, attrdef_t *attrdef);
ICACHE_FLASH_ATTR static void       attr_set_param_num_choices(port_t *port, attrdef_t *attrdef, int index);

ICACHE_FLASH_ATTR static void       attr_set_user_data_cache(port_t *port, attrdef_t *attrdef, double value);

ICACHE_FLASH_ATTR static void       port_load(port_t *port, uint8 *config_data);
ICACHE_FLASH_ATTR static void       port_save(port_t *port, uint8 *config_data, uint32 *strings_offs);


int64 attr_get_param_uint8(port_t *port, attrdef_t *attrdef) {
    uint8 v = PERIPHERAL_PARAM_UINT8(port->peripheral, attrdef->storage_param_no);

    if (attrdef->cache_user_data_field_offs) {
        attr_set_user_data_cache(port, attrdef, v);
    }

    return v;
}

void attr_set_param_uint8(port_t *port, attrdef_t *attrdef, int64 value) {
    uint8 v = value;

    if (attrdef->cache_user_data_field_offs) {
        attr_set_user_data_cache(port, attrdef, v);
    }

    PERIPHERAL_PARAM_UINT8(port->peripheral, attrdef->storage_param_no) = v;
}

int64 attr_get_param_sint8(port_t *port, attrdef_t *attrdef) {
    int8 v = PERIPHERAL_PARAM_SINT8(port->peripheral, attrdef->storage_param_no);

    if (attrdef->cache_user_data_field_offs) {
        attr_set_user_data_cache(port, attrdef, v);
    }

    return v;
}

void attr_set_param_sint8(port_t *port, attrdef_t *attrdef, int64 value) {
    int8 v = value;

    if (attrdef->cache_user_data_field_offs) {
        attr_set_user_data_cache(port, attrdef, v);
    }

    PERIPHERAL_PARAM_SINT8(port->peripheral, attrdef->storage_param_no) = v;
}

int64 attr_get_param_uint16(port_t *port, attrdef_t *attrdef) {
    uint16 v = PERIPHERAL_PARAM_UINT16(port->peripheral, attrdef->storage_param_no);

    if (attrdef->cache_user_data_field_offs) {
        attr_set_user_data_cache(port, attrdef, v);
    }

    return v;
}

void attr_set_param_uint16(port_t *port, attrdef_t *attrdef, int64 value) {
    uint16 v = value;

    if (attrdef->cache_user_data_field_offs) {
        attr_set_user_data_cache(port, attrdef, v);
    }

    PERIPHERAL_PARAM_UINT16(port->peripheral, attrdef->storage_param_no) = v;
}

int64 attr_get_param_sint16(port_t *port, attrdef_t *attrdef) {
    int16 v = PERIPHERAL_PARAM_SINT16(port->peripheral, attrdef->storage_param_no);

    if (attrdef->cache_user_data_field_offs) {
        attr_set_user_data_cache(port, attrdef, v);
    }

    return v;
}

void attr_set_param_sint16(port_t *port, attrdef_t *attrdef, int64 value) {
    int16 v = value;

    if (attrdef->cache_user_data_field_offs) {
        attr_set_user_data_cache(port, attrdef, v);
    }

    PERIPHERAL_PARAM_SINT16(port->peripheral, attrdef->storage_param_no) = v;
}

int64 attr_get_param_uint32(port_t *port, attrdef_t *attrdef) {
    uint32 v = PERIPHERAL_PARAM_UINT32(port->peripheral, attrdef->storage_param_no);

    if (attrdef->cache_user_data_field_offs) {
        attr_set_user_data_cache(port, attrdef, v);
    }

    return v;
}

void attr_set_param_uint32(port_t *port, attrdef_t *attrdef, int64 value) {
    uint32 v = value;

    if (attrdef->cache_user_data_field_offs) {
        attr_set_user_data_cache(port, attrdef, v);
    }

    PERIPHERAL_PARAM_SINT32(port->peripheral, attrdef->storage_param_no) = v;
}

int64 attr_get_param_sint32(port_t *port, attrdef_t *attrdef) {
    int32 v = PERIPHERAL_PARAM_SINT32(port->peripheral, attrdef->storage_param_no);

    if (attrdef->cache_user_data_field_offs) {
        attr_set_user_data_cache(port, attrdef, v);
    }

    return v;
}

void attr_set_param_sint32(port_t *port, attrdef_t *attrdef, int64 value) {
    int32 v = value;

    if (attrdef->cache_user_data_field_offs) {
        attr_set_user_data_cache(port, attrdef, v);
    }

    PERIPHERAL_PARAM_UINT16(port->peripheral, attrdef->storage_param_no) = v;
}

int64 attr_get_param_sint64(port_t *port, attrdef_t *attrdef) {
    int64 v = PERIPHERAL_PARAM_SINT64(port->peripheral, attrdef->storage_param_no);

    if (attrdef->cache_user_data_field_offs) {
        attr_set_user_data_cache(port, attrdef, v);
    }

    return v;
}

void attr_set_param_sint64(port_t *port, attrdef_t *attrdef, int64 value) {
    if (attrdef->cache_user_data_field_offs) {
        attr_set_user_data_cache(port, attrdef, value);
    }

    PERIPHERAL_PARAM_SINT64(port->peripheral, attrdef->storage_param_no) = value;
}

double attr_get_param_double(port_t *port, attrdef_t *attrdef) {
    double v = PERIPHERAL_PARAM_DOUBLE(port->peripheral, attrdef->storage_param_no);

    if (attrdef->cache_user_data_field_offs) {
        attr_set_user_data_cache(port, attrdef, v);
    }

    return v;
}

void attr_set_param_double(port_t *port, attrdef_t *attrdef, double value) {
    if (attrdef->cache_user_data_field_offs) {
        attr_set_user_data_cache(port, attrdef, value);
    }

    PERIPHERAL_PARAM_UINT16(port->peripheral, attrdef->storage_param_no) = value;
}

int attr_get_flag(port_t *port, attrdef_t *attrdef) {
    bool v = PERIPHERAL_GET_FLAG(port->peripheral, attrdef->storage_flag_bit_no);

    if (attrdef->cache_user_data_field_offs) {
        attr_set_user_data_cache(port, attrdef, v);
    }

    return v;
}

void attr_set_flag(port_t *port, attrdef_t *attrdef, int value) {
    bool v = value;

    if (attrdef->cache_user_data_field_offs) {
        attr_set_user_data_cache(port, attrdef, v);
    }

    PERIPHERAL_SET_FLAG(port->peripheral, attrdef->storage_flag_bit_no, v);
}

int attr_get_param_num_choices(port_t *port, attrdef_t *attrdef) {
    uint8 index = PERIPHERAL_PARAM_UINT8(port->peripheral, attrdef->storage_param_no);

    if (attrdef->cache_user_data_field_offs) {
        double value = get_choice_value_num(attrdef->choices[index]);
        attr_set_user_data_cache(port, attrdef, value);
    }

    return index;
}

void attr_set_param_num_choices(port_t *port, attrdef_t *attrdef, int index) {
    PERIPHERAL_PARAM_UINT8(port->peripheral, attrdef->storage_param_no) = index;

    if (attrdef->cache_user_data_field_offs) {
        double value = get_choice_value_num(attrdef->choices[index]);
        attr_set_user_data_cache(port, attrdef, value);
    }
}

void attr_set_user_data_cache(port_t *port, attrdef_t *attrdef, double value) {
    void *dest_addr = ((uint8 *) port->peripheral->user_data) + attrdef->cache_user_data_field_offs - 1;
    switch (attrdef->storage) {
        case ATTRDEF_STORAGE_PARAM_UINT8: {
            uint8 v = value;
            memcpy(dest_addr, &v, 1);
            break;
        }

        case ATTRDEF_STORAGE_PARAM_SINT8: {
            int8 v = value;
            memcpy(dest_addr, &v, 1);
            break;
        }

        case ATTRDEF_STORAGE_PARAM_UINT16: {
            uint16 v = value;
            memcpy(dest_addr, &v, 2);
            break;
        }

        case ATTRDEF_STORAGE_PARAM_SINT16: {
            sint16 v = value;
            memcpy(dest_addr, &v, 2);
            break;
        }

        case ATTRDEF_STORAGE_PARAM_UINT32: {
            uint32 v = value;
            memcpy(dest_addr, &v, 4);
            break;
        }

        case ATTRDEF_STORAGE_PARAM_SINT32: {
            int32 v = value;
            memcpy(dest_addr, &v, 4);
            break;
        }

        case ATTRDEF_STORAGE_PARAM_SINT64: {
            int64 v = value;
            memcpy(dest_addr, &v, 8);
            break;
        }

        case ATTRDEF_STORAGE_PARAM_DOUBLE: {
            memcpy(dest_addr, &value, 8);
            break;
        }
    }
}

void port_load(port_t *port, uint8 *config_data) {
    uint8 *base_ptr = config_data + CONFIG_OFFS_PORT_BASE + CONFIG_PORT_SIZE * port->slot;
    char *strings_ptr = (char *) config_data + CONFIG_OFFS_STR_BASE;

    /* id */
    if (port->id) {
        free(port->id);
    }
    port->id = string_pool_read_dup(strings_ptr, base_ptr + PORT_CONFIG_OFFS_ID);
    if (!port->id) {
        char dummy_id[7];
        snprintf(dummy_id, sizeof(dummy_id), "port%02d", port->slot);
        port->id = strdup(dummy_id);
    }
    DEBUG_PORT(port, "id = \"%s\"", port->id);

    DEBUG_PORT(port, "slot = %d", port->slot);

    /* display_name */
    port->display_name = string_pool_read_dup(strings_ptr, base_ptr + PORT_CONFIG_OFFS_DISP_NAME);
    DEBUG_PORT(port, "display_name = \"%s\"", port->display_name ? port->display_name : "");

    /* unit */
    if (port->type == PORT_TYPE_NUMBER) {
        if (string_pool_read(strings_ptr, base_ptr + PORT_CONFIG_OFFS_UNIT)) {
            if (port->unit) {
                free(port->unit);
            }
            port->unit = string_pool_read_dup(strings_ptr, base_ptr + PORT_CONFIG_OFFS_UNIT);
        }
        DEBUG_PORT(port, "unit = \"%s\"", port->unit ? port->unit : "");
    }

    /* flags */
    uint32 flags = 0;
    memcpy(&flags, base_ptr + PORT_CONFIG_OFFS_FLAGS, 4);
    port->flags |= flags; /* Flags that are set by initialization remain set */

    /* All enabled ports are automatically considered as set */
    if (IS_PORT_ENABLED(port)) {
        port->flags |= PORT_FLAG_SET;
    }

    DEBUG_PORT(port, "enabled = %s", IS_PORT_ENABLED(port) ? "true" : "false");
    DEBUG_PORT(port, "writable = %s", IS_PORT_WRITABLE(port) ? "true" : "false");
    DEBUG_PORT(port, "set = %s", IS_PORT_SET(port) ? "true" : "false");
    DEBUG_PORT(port, "persisted = %s", IS_PORT_PERSISTED(port) ? "true" : "false");
    DEBUG_PORT(port, "internal = %s", IS_PORT_INTERNAL(port) ? "true" : "false");
    DEBUG_PORT(port, "virtual = %s", IS_PORT_VIRTUAL(port) ? "true" : "false");

    /* value */
    double value = UNDEFINED;
    if (IS_PORT_PERSISTED(port)) {
        memcpy(&value, base_ptr + PORT_CONFIG_OFFS_VALUE, sizeof(double));

        if (IS_UNDEFINED(value)) {
            DEBUG_PORT(port, "value = (undefined)");
        }
        else {
            DEBUG_PORT(port, "value = %s", dtostr(value, -1));
        }
    }

    /* sampling_interval */
    memcpy(&port->sampling_interval, base_ptr + PORT_CONFIG_OFFS_SAMP_INT, 4);
    if (!port->def_sampling_interval) {
        port->def_sampling_interval = PORT_DEF_SAMP_INT;
    }
    if (!port->sampling_interval) {
        port->sampling_interval = port->def_sampling_interval;
    }
    if (!port->max_sampling_interval) {
        port->max_sampling_interval = PORT_MAX_SAMP_INT;
    }
    port->last_sample_time = -LLONG_MAX;

    DEBUG_PORT(port, "sampling_interval = %d ms", port->sampling_interval);

    /* Heart beat */
    if (!port->heart_beat_interval) {
        port->heart_beat_interval = PORT_DEF_HEART_BEAT_INT;
    }
    port->last_heart_beat_time = -LLONG_MAX;

    DEBUG_PORT(port, "heart_beat_interval = %d ms", port->heart_beat_interval);

    /* Expressions */
    port->sexpr = NULL;
    port->expr = NULL;
    port->stransform_write = NULL;
    port->transform_write = NULL;
    port->stransform_read = NULL;
    port->transform_read = NULL;

    if (IS_PORT_WRITABLE(port)) {
        /* Value expression */
        port->sexpr = string_pool_read_dup(strings_ptr, base_ptr + PORT_CONFIG_OFFS_EXPR);
        DEBUG_PORT(port, "expression = \"%s\"", port->sexpr ? port->sexpr : "");
        /* The expression will be parsed later, in config_init() */

        /* transform_write */
        port->stransform_write = string_pool_read_dup(strings_ptr, base_ptr + PORT_CONFIG_OFFS_TRANS_W);
        DEBUG_PORT(port, "transform_write = \"%s\"", port->stransform_write ? port->stransform_write : "");

        if (port->stransform_write) {
            port->transform_write = expr_parse(port->id, port->stransform_write, strlen(port->stransform_write));
            if (port->transform_write) {
                DEBUG_PORT(port, "write transform successfully parsed");
            }
            else {
                DEBUG_PORT(port, "write transform parse failed");
                free(port->stransform_write);
                port->stransform_write = NULL;
            }
        }
    }

    /* transform_read */
    port->stransform_read = string_pool_read_dup(strings_ptr, base_ptr + PORT_CONFIG_OFFS_TRANS_R);
    DEBUG_PORT(port, "transform_read = \"%s\"", port->stransform_read ? port->stransform_read : "");

    if (port->stransform_read) {
        port->transform_read = expr_parse(port->id, port->stransform_read, strlen(port->stransform_read));
        if (port->transform_read) {
            DEBUG_PORT(port, "read transform successfully parsed");
        }
        else {
            DEBUG_PORT(port, "read transform parse failed");
            free(port->stransform_read);
            port->stransform_read = NULL;
        }
    }

    /* Sequence */
    port->sequence_pos = -1;

    /* Prepare custom attributes */
    if (port->attrdefs) {
        attrdef_t *a, **attrdefs = port->attrdefs;
        while ((a = *attrdefs++)) {
            if (!IS_PORT_SET(port)) {
                DEBUG_PORT(port, "setting default value for attribute %s", a->name);

                switch (a->type) {
                    case ATTR_TYPE_BOOLEAN: {
                        bool value = a->def_bool;
                        ((int_setter_t) a->set)(port, a, value);

                        DEBUG_PORT(port, "%s set to %d", a->name, value);

                        break;
                    }

                    case ATTR_TYPE_NUMBER: {
                        if (a->choices) {
                            ((int_setter_t) a->set)(port, a, (int) a->def);
                            DEBUG_PORT(port, "%s set to choice %d", a->name, (int) a->def);
                        }
                        else {
                            if (IS_ATTRDEF_INTEGER(a)) {
                                ((int_setter_t) a->set)(port, a, (int) a->def);
                                DEBUG_PORT(port, "%s set to %d", a->name, (int) a->def);
                            }
                            else { /* float */
                                ((float_setter_t) a->set)(port, a, a->def);
                                DEBUG_PORT(port, "%s set to choice %s", a->name, dtostr(a->def, -1));
                            }
                        }

                        break;
                    }

                    case ATTR_TYPE_STRING: {
                        if (a->choices) {
                            ((int_setter_t) a->set)(port, a, (int) a->def);
                            DEBUG_PORT(port, "%s set to choice %d", a->name, (int) a->def);
                        }
                        else if (a->def_str) {
                            ((str_setter_t) a->set)(port, a, a->def_str);
                            DEBUG_PORT(port, "%s set to %s", a->name, a->def_str);
                        }

                        break;
                    }
                }
            }

            /* Calling getter will load value into cache */
            if (a->cache_user_data_field_offs) {
                ((int_getter_t) a->get)(port, a);
            }
        }
    }

    /* Port can now be configured */
    if (IS_PORT_ENABLED(port)) {
        port_configure(port);
    }

    /* Initial port value */
    port->value = UNDEFINED;
    if (IS_PORT_WRITABLE(port) && IS_PORT_ENABLED(port)) {
        if (IS_PORT_PERSISTED(port)) {
            /* Initial value is given by the persisted value */
            port->value = value;
            DEBUG_PORT(port, "setting persisted value %s", dtostr(port->value, -1));
        }
        else {
            /* Initial value is given by the read value */
            port->value = port->read_value(port);
            if (!IS_UNDEFINED(port->value)) {
                if (port->transform_read) {
                    port->value = expr_eval(port->transform_read);
                }

                DEBUG_PORT(port, "setting read value %s", dtostr(port->value, -1));
            }
        }

        if (!IS_UNDEFINED(port->value)) {
            port_set_value(port, port->value, CHANGE_REASON_NATIVE);
        }
    }

    port->change_reason = CHANGE_REASON_NATIVE;

    /* From now on, consider port configured */
    port->flags |= PORT_FLAG_SET;
}

void port_save(port_t *port, uint8 *config_data, uint32 *strings_offs) {
    uint8 *base_ptr = config_data + CONFIG_OFFS_PORT_BASE + CONFIG_PORT_SIZE * port->slot;
    char *strings_ptr = (char *) config_data + CONFIG_OFFS_STR_BASE;

    /* id */
    if (!string_pool_write(strings_ptr, strings_offs, port->id, base_ptr + PORT_CONFIG_OFFS_ID)) {
        DEBUG_PORT(port, "no more strings pool space");
    }

    /* display name */
    if (!string_pool_write(strings_ptr, strings_offs, port->display_name, base_ptr + PORT_CONFIG_OFFS_DISP_NAME)) {
        DEBUG_PORT(port, "no more strings pool space");
    }

    /* unit */
    if (port->type == PORT_TYPE_NUMBER) {
        if (!string_pool_write(strings_ptr, strings_offs, port->unit, base_ptr + PORT_CONFIG_OFFS_UNIT)) {
            DEBUG_PORT(port, "no more strings pool space");
        }
    }

    /* Flags */
    memcpy(base_ptr + PORT_CONFIG_OFFS_FLAGS, &port->flags, 4);

    /* value */
    memcpy(base_ptr + PORT_CONFIG_OFFS_VALUE, &port->value, sizeof(double));

    /* min */
    memcpy(base_ptr + PORT_CONFIG_OFFS_MIN, &port->min, sizeof(double));

    /* max */
    memcpy(base_ptr + PORT_CONFIG_OFFS_MAX, &port->max, sizeof(double));

    /* step */
    memcpy(base_ptr + PORT_CONFIG_OFFS_STEP, &port->step, sizeof(double));

    /* sampling_interval */
    memcpy(base_ptr + PORT_CONFIG_OFFS_SAMP_INT, &port->sampling_interval, 4);

    /* value expression */
    if (!string_pool_write(strings_ptr, strings_offs, port->sexpr, base_ptr + PORT_CONFIG_OFFS_EXPR)) {
        DEBUG_PORT(port, "no more strings pool space");
    }

    /* transform_write */
    if (!string_pool_write(strings_ptr, strings_offs, port->stransform_write, base_ptr + PORT_CONFIG_OFFS_TRANS_W)) {
        DEBUG_PORT(port, "no more strings pool space");
    }

    /* transform_read */
    if (!string_pool_write(strings_ptr, strings_offs, port->stransform_read, base_ptr + PORT_CONFIG_OFFS_TRANS_R)) {
        DEBUG_PORT(port, "no more strings pool space");
    }

    /* choices */
    if (port->choices) {
        int len;
        char *c, *choices_str = zalloc(1);
        char **choices = port->choices;
        len = 0;
        while ((c = *choices++)) {
            len += strlen(c) + 1;
            choices_str = realloc(choices_str, len + 1);
            strcat(choices_str, c);
            choices_str[len - 1] = '|';
            choices_str[len] = 0;
        }

        choices_str[len - 1] = 0;

        if (!string_pool_write(strings_ptr, strings_offs, choices_str, base_ptr + PORT_CONFIG_OFFS_CHOICES)) {
            DEBUG_PORT(port, "no more strings pool space");
        }

        free(choices_str);
    }
    else {
        string_pool_write(strings_ptr, strings_offs, NULL, base_ptr + PORT_CONFIG_OFFS_CHOICES);
    }
}

void ports_init(uint8 *config_data) {
#ifdef _VIRTUAL
    virtual_ports_init(config_data);
#endif

    /* Load port data */
    port_t *p;
    int i;
    for (i = 0; i < all_ports_count; i++) {
        p = all_ports[i];
        DEBUG_PORT(p, "loading data");
        port_load(p, config_data);
    }
}

void ports_save(uint8 *config_data, uint32 *strings_offs) {
    port_t *p;
    int i;
    for (i = 0; i < all_ports_count; i++) {
        p = all_ports[i];
        DEBUG_PORT(p, "saving");
        port_save(p, config_data, strings_offs);
    }

    virtual_ports_save(config_data, strings_offs);
}

bool ports_slot_busy(uint8 slot) {
    return !!(used_slots & (1 << slot));
}

int8 ports_next_slot() {
    uint8 slot;

    /* Virtual port slots come in last and are not available for peripherals. Always look for free ports starting with
     * extra slots. If no extra slot is available, look through standard slots as well. */

    for (slot = PORT_SLOT_EXTRA0; slot < PORT_SLOT_VIRTUAL0; slot++) {
        if (!(used_slots & (1 << slot))) {
            return slot;
        }
    }

    /* If no extra slot is free, try the regular slots */
    for (slot = 0; slot < PORT_SLOT_EXTRA0; slot++) {
        if (!(used_slots & (1 << slot))) {
            return slot;
        }
    }

    return -1;
}

void port_register(port_t *port) {
    /* If a unit is present, it's normally a literal string. Make sure it's a free()-able string. */
    if (port->unit) {
        port->unit = strdup(port->unit);
    }

    all_ports = realloc(all_ports, (all_ports_count + 1) * sizeof(port_t *));
    all_ports[all_ports_count++] = port;

    /* Prepare custom port attrdefs */
    if (port->attrdefs) {
        attrdef_t *a, **attrdefs = port->attrdefs;
        while ((a = *attrdefs++)) {
            /* Set min/max to UNDEFINED, by default */
            if (a->min == 0 && a->max == 0) {
                a->min = a->max = UNDEFINED;
            }

            /* Set default getter/setter */
            /* TODO: add support for default setters/getters for string attributes */
            if ((!a->get || !a->set) && (a->storage != ATTRDEF_STORAGE_CUSTOM)) {
                if (a->choices) {
                    if (a->type == ATTR_TYPE_NUMBER) {
                        a->get = attr_get_param_num_choices;
                        a->set = attr_set_param_num_choices;
                    }
                }
                else {
                    switch (a->storage) {
                        case ATTRDEF_STORAGE_PARAM_UINT8:
                            a->get = attr_get_param_uint8;
                            a->set = attr_set_param_uint8;
                            break;

                        case ATTRDEF_STORAGE_PARAM_SINT8:
                            a->get = attr_get_param_sint8;
                            a->set = attr_set_param_sint8;
                            break;

                        case ATTRDEF_STORAGE_PARAM_UINT16:
                            a->get = attr_get_param_uint16;
                            a->set = attr_set_param_uint16;
                            break;

                        case ATTRDEF_STORAGE_PARAM_SINT16:
                            a->get = attr_get_param_sint16;
                            a->set = attr_set_param_sint16;
                            break;

                        case ATTRDEF_STORAGE_PARAM_UINT32:
                            a->get = attr_get_param_uint32;
                            a->set = attr_set_param_uint32;
                            break;

                        case ATTRDEF_STORAGE_PARAM_SINT32:
                            a->get = attr_get_param_sint32;
                            a->set = attr_set_param_sint32;
                            break;

                        case ATTRDEF_STORAGE_PARAM_SINT64:
                            a->get = attr_get_param_sint64;
                            a->set = attr_set_param_sint64;
                            break;

                        case ATTRDEF_STORAGE_PARAM_DOUBLE:
                            a->get = attr_get_param_double;
                            a->set = attr_set_param_double;
                            break;

                        case ATTRDEF_STORAGE_FLAG:
                            a->get = attr_get_flag;
                            a->set = attr_set_flag;
                            break;
                    }
                }
            }
        }
    }

    used_slots |= 1L << port->slot;

    if (!port->id) { /* Port may not have an ID when registered */
        char dummy_id[7];
        snprintf(dummy_id, sizeof(dummy_id), "port%02d", port->slot);
        port->id = strdup(dummy_id);
    }

    DEBUG_PORT(port, "registered");
}

bool port_unregister(port_t *port) {
    int i, p = -1;
    for (i = 0; i < all_ports_count; i++) {
        if (all_ports[i] == port) {
            p = i;
            break;
        }
    }

    if (p == -1) {
        DEBUG_PORT(port, "not found in all ports list");
        return FALSE;  /* Port not found */
    }

    /* Shift following ports back by 1 position */
    for (i = p; i < all_ports_count - 1; i++) {
        all_ports[i] = all_ports[i + 1];
    }

    if (all_ports_count > 1) {
        all_ports = realloc(all_ports, --all_ports_count * sizeof(port_t *));
    }
    else {
        free(all_ports);
        all_ports = NULL;
        all_ports_count = 0;
    }

    used_slots &= ~(1L << port->slot);

    DEBUG_PORT(port, "unregistered");

    return TRUE;
}

void port_cleanup(port_t *port) {
    DEBUG_PORT(port, "cleaning up");

    /* Free choices */
    if (port->choices) {
        free_choices(port->choices);
        port->choices = NULL;
    }

    /* Free ID */
    if (port->id) {
        free(port->id);
        port->id= NULL;
    }
    /* Free display name */
    if (port->display_name) {
        free(port->display_name);
        port->display_name = NULL;
    }
    /* Free unit */
    if (port->unit) {
        free(port->unit);
        port->unit = NULL;
    }

    /* Destroy value expression */
    if (port->expr) {
        port_expr_remove(port);
    }

    /* Destroy transform expressions */
    if (port->transform_read) {
        expr_free(port->transform_read);
        port->transform_read = NULL;
    }
    if (port->stransform_read) {
        free(port->stransform_read);
        port->stransform_read = NULL;
    }
    if (port->transform_write) {
        expr_free(port->transform_write);
        port->transform_write = NULL;
    }
    if (port->stransform_write) {
        free(port->stransform_write);
        port->stransform_write = NULL;
    }

    /* Cancel sequence */
    if (port->sequence_pos >= 0) {
        port_sequence_cancel(port);
    }
}

port_t *port_find_by_id(char *id) {
    port_t *p;
    int i;
    for (i = 0; i < all_ports_count; i++) {
        p = all_ports[i];
        if (!strcmp(p->id, id)) {
            return p;
        }
    }

    return NULL;    
}

void port_rebuild_change_dep_mask(port_t *the_port) {
    the_port->change_dep_mask = 0;

    if (!the_port->expr) {
        return;
    }

    port_t **ports, **port, *p;
    port = ports = expr_port_deps(the_port->expr);
    if (ports) {
        while ((p = *port++)) {
            the_port->change_dep_mask |= (1ULL << p->slot);
        }
        free(ports);
    }

    if (expr_is_time_dep(the_port->expr)) {
        the_port->change_dep_mask |= (1ULL << TIME_EXPR_DEP_BIT);
    }
    if (expr_is_time_ms_dep(the_port->expr)) {
        the_port->change_dep_mask |= (1ULL << TIME_MS_EXPR_DEP_BIT);
    }

    DEBUG_PORT(the_port, "change dependency mask is " FMT_UINT64_HEX, FMT_UINT64_VAL(the_port->change_dep_mask));
}

void port_sequence_cancel(port_t *port) {
    DEBUG_PORT(port, "canceling sequence");
    free(port->sequence_values);
    free(port->sequence_delays);
    port->sequence_pos = -1;
    port->sequence_repeat = -1;
    os_timer_disarm(&port->sequence_timer);
}

void port_expr_remove(port_t *port) {
    DEBUG_PORT(port, "removing expression");

    if (port->expr) {
        expr_free(port->expr);
        port->expr = NULL;
    }

    if (port->sexpr) {
        free(port->sexpr);
        port->sexpr = NULL;
    }
}

bool port_set_value(port_t *port, double value, char reason) {
    if (port->transform_write) {
        /* Temporarily set the port value to the new value, so that the write transform expression takes the new value
         * into consideration when evaluating the result */
        double old_value = port->value;
        port->value = value;
        value = expr_eval(port->transform_write);
        port->value = old_value;

        /* Ignore invalid values yielded by transform expression */
        if (IS_UNDEFINED(value)) {
            return FALSE;
        }
    }

    DEBUG_PORT(port, "setting value %s, reason = %c", dtostr(value, -1), reason);

    bool result = port->write_value(port, value);
    if (!result) {
        DEBUG_PORT(port, "setting value failed");
    }

    port->change_reason = reason;

    return result;
}

json_t *port_get_json_value(port_t *port) {
    if (IS_UNDEFINED(port->value) || !IS_PORT_ENABLED(port)) {
        return json_null_new();
    }

    switch (port->type) {
        case PORT_TYPE_BOOLEAN:
            return json_bool_new(port->value);

        case PORT_TYPE_NUMBER:
            return json_double_new(port->value);
    }

    return NULL;
}

void port_enable(port_t *port) {
    DEBUG_PORT(port, "enabling");
    port->flags |= PORT_FLAG_ENABLED;

    /* Rebuild expression */
    if (port->expr) {
        expr_free(port->expr);
        port->expr = NULL;
    }
    if (port->sexpr) {
        port->expr = expr_parse(port->id, port->sexpr, strlen(port->sexpr));
        if (port->expr) {
            DEBUG_PORT(port, "value expression successfully parsed");
        }
        else {
            DEBUG_PORT(port, "value expression parse failed");
            free(port->sexpr);
            port->sexpr = NULL;
        }

        update_port_expression(port);
    }

    port_rebuild_change_dep_mask(port);
}

void port_disable(port_t *port) {
    DEBUG_PORT(port, "disabling");
    port->flags &= ~PORT_FLAG_ENABLED;

    /* Destroy value expression */
    if (port->expr) {
        expr_free(port->expr);
        port->expr = NULL;
    }

    /* Cancel sequence */
    if (port->sequence_pos >= 0) {
        port_sequence_cancel(port);
    }
}

void port_configure(port_t *port) {
    DEBUG_PORT(port, "configuring");

    /* Attribute getters may cache values inside port->user_data; calling all attribute getters here ensures that this
     * cached data is up-do-date with latest attribute values */
    if (port->attrdefs) {
        attrdef_t *a, **attrdefs = port->attrdefs;
        while ((a = *attrdefs++)) {
            if (a->get) {
                ((int_getter_t) a->get)(port, a);
            }
        }
    }

    if (port->configure) {
        port->configure(port);
    }
}
