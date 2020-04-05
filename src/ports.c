
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
#include "espgoodies/utils.h"

#include "common.h"
#include "config.h"
#include "core.h"
#include "events.h"
#include "ports.h"
#include "stringpool.h"


ICACHE_FLASH_ATTR static void       port_load(port_t *port, uint8 *data);
ICACHE_FLASH_ATTR static void       port_save(port_t *port, uint8 *data, uint32 *strings_offs);


port_t                           ** all_ports = NULL;
char                              * all_gpio_choices[] = {"0:GPIO0", "1:GPIO1", "2:GPIO2", "3:GPIO3", "4:GPIO4",
                                                          "5:GPIO5", "6:GPIO6", "7:GPIO7", "8:GPIO8", "9:GPIO9",
                                                          "10:GPIO10", "11:GPIO11", "12:GPIO12", "13:GPIO13",
                                                          "14:GPIO14", "15:GPIO15", "16:GPIO16", NULL};

char                              * all_gpio_none_choices[] = {"0:GPIO0", "1:GPIO1", "2:GPIO2", "3:GPIO3", "4:GPIO4",
                                                              "5:GPIO5", "6:GPIO6", "7:GPIO7", "8:GPIO8", "9:GPIO9",
                                                              "10:GPIO10", "11:GPIO11", "12:GPIO12", "13:GPIO13",
                                                              "14:GPIO14", "15:GPIO15", "16:GPIO16", "-1:none", NULL};

char                              * esp8266_gpio_choices[] = {"0:GPIO0", "1:GPIO1", "2:GPIO2", "3:GPIO3", "4:GPIO4",
                                                              "5:GPIO5", "12:GPIO12", "13:GPIO13", "14:GPIO14",
                                                              "15:GPIO15", "16:GPIO16", NULL};

char                              * esp8266_gpio_none_choices[] = {"0:GPIO0", "1:GPIO1", "2:GPIO2", "3:GPIO3",
                                                                  "4:GPIO4", "5:GPIO5", "12:GPIO12", "13:GPIO13",
                                                                  "14:GPIO14", "15:GPIO15", "16:GPIO16", "-1:none",
                                                                  NULL};

static int                          all_ports_count = 0;
static uint8                        next_extra_slot = PORT_SLOT_EXTRA0;


void port_load(port_t *port, uint8 *data) {
    uint8 *base_ptr = data + CONFIG_OFFS_PORT_BASE + CONFIG_PORT_SIZE * port->slot;
    char *strings_ptr = (char *) data + CONFIG_OFFS_STR_BASE;

    DEBUG_PORT(port, "slot = %d", port->slot);

    /* display_name */
    port->display_name = string_pool_read_dup(strings_ptr, base_ptr + CONFIG_OFFS_PORT_DISP_NAME);
    DEBUG_PORT(port, "display_name = \"%s\"", port->display_name ? port->display_name : "");

    /* unit */
    if (port->type == PORT_TYPE_NUMBER) {
        if (string_pool_read(strings_ptr, base_ptr + CONFIG_OFFS_PORT_UNIT)) {
            if (port->unit) {
                free(port->unit);
            }
            port->unit = string_pool_read_dup(strings_ptr, base_ptr + CONFIG_OFFS_PORT_UNIT);
        }
        DEBUG_PORT(port, "unit = \"%s\"", port->unit ? port->unit : "");
    }

    /* flags */
    bool initially_output = port->flags & PORT_FLAG_OUTPUT;
    memcpy(&port->flags, base_ptr + CONFIG_OFFS_PORT_FLAGS, 4);
    if (initially_output) {
        /* If port is defined as output, ignore persisted output flag */
        port->flags |= PORT_FLAG_OUTPUT;
    }

    DEBUG_PORT(port, "enabled = %s", IS_ENABLED(port) ? "true" : "false");
    DEBUG_PORT(port, "output = %s", IS_OUTPUT(port) ? "true" : "false");
    DEBUG_PORT(port, "pull_up = %s", IS_PULL_UP(port) ? "true" : "false");
    DEBUG_PORT(port, "persisted = %s", IS_PERSISTED(port) ? "true" : "false");
    DEBUG_PORT(port, "virtual = %s", IS_VIRTUAL(port) ? "true" : "false");

    /* value */
    double value = UNDEFINED;
    if (IS_PERSISTED(port)) {
        memcpy(&value, base_ptr + CONFIG_OFFS_PORT_VALUE, sizeof(double));

        if (IS_UNDEFINED(value)) {
            DEBUG_PORT(port, "value = (undefined)");
        }
        else {
            DEBUG_PORT(port, "value = %s", dtostr(value, -1));
        }
    }

    /* sampling_interval */
    memcpy(&port->sampling_interval, base_ptr + CONFIG_OFFS_PORT_SAMP_INT, 4);
    if (!port->def_sampling_interval) {
        port->def_sampling_interval = PORT_DEF_SAMP_INT;
    }
    if (!port->sampling_interval) {
        port->sampling_interval = port->def_sampling_interval;
    }
    if (!port->max_sampling_interval) {
        port->max_sampling_interval = PORT_MAX_SAMP_INT;
    }
    port->last_sample_time = -INT_MAX;

    DEBUG_PORT(port, "sampling_interval = %d ms", port->sampling_interval);

    /* Heart beat */
    if (!port->heart_beat_interval) {
        port->heart_beat_interval = PORT_DEF_HEART_BEAT_INT;
    }
    port->last_heart_beat_time = -INT_MAX;

    DEBUG_PORT(port, "heart_beat_interval = %d ms", port->heart_beat_interval);

    /* Custom data */
    memcpy(port->extra_data, base_ptr + CONFIG_OFFS_PORT_DATA, PORT_PERSISTED_EXTRA_DATA_LEN);

    port->sexpr = NULL;
    port->expr = NULL;
    port->stransform_write = NULL;
    port->transform_write = NULL;
    port->stransform_read = NULL;
    port->transform_read = NULL;

    if (IS_OUTPUT(port)) {
        /* Value expression */
        port->sexpr = string_pool_read_dup(strings_ptr, base_ptr + CONFIG_OFFS_PORT_EXPR);
        DEBUG_PORT(port, "expression = \"%s\"", port->sexpr ? port->sexpr : "");
        /* The expression will be parsed later, in config_init() */

        /* transform_write */
        port->stransform_write = string_pool_read_dup(strings_ptr, base_ptr + CONFIG_OFFS_PORT_TRANS_W);
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
    port->stransform_read = string_pool_read_dup(strings_ptr, base_ptr + CONFIG_OFFS_PORT_TRANS_R);
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

    if (IS_ENABLED(port)) {
        port_configure(port);
    }

    /* Initial port value */
    port->value = UNDEFINED;
    if (IS_OUTPUT(port) && IS_ENABLED(port)) {
        if (IS_PERSISTED(port)) {
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
}

void port_save(port_t *port, uint8 *data, uint32 *strings_offs) {
    uint8 *base_ptr = data + CONFIG_OFFS_PORT_BASE + CONFIG_PORT_SIZE * port->slot;
    char *strings_ptr = (char *) data + CONFIG_OFFS_STR_BASE;

    /* id */
    if (IS_VIRTUAL(port)) {
        /* Only virtual ports have custom ids */
        if (!string_pool_write(strings_ptr, strings_offs, port->id, base_ptr + CONFIG_OFFS_PORT_ID)) {
            DEBUG_PORT(port, "no more string space to save id");
        }
    }

    /* display name */
    if (!string_pool_write(strings_ptr, strings_offs, port->display_name, base_ptr + CONFIG_OFFS_PORT_DISP_NAME)) {
        DEBUG_PORT(port, "no more string space to save display name");
    }

    /* unit */
    if (port->type == PORT_TYPE_NUMBER) {
        if (!string_pool_write(strings_ptr, strings_offs, port->unit, base_ptr + CONFIG_OFFS_PORT_UNIT)) {
            DEBUG_PORT(port, "no more string space to save unit");
        }
    }

    /* flags */
    memcpy(base_ptr + CONFIG_OFFS_PORT_FLAGS, &port->flags, 4);

    /* value */
    memcpy(base_ptr + CONFIG_OFFS_PORT_VALUE, &port->value, sizeof(double));

    /* min */
    memcpy(base_ptr + CONFIG_OFFS_PORT_MIN, &port->min, sizeof(double));

    /* max */
    memcpy(base_ptr + CONFIG_OFFS_PORT_MAX, &port->max, sizeof(double));

    /* step */
    memcpy(base_ptr + CONFIG_OFFS_PORT_STEP, &port->step, sizeof(double));

    /* sampling_interval */
    memcpy(base_ptr + CONFIG_OFFS_PORT_SAMP_INT, &port->sampling_interval, 4);

    /* Custom data */
    memcpy(base_ptr + CONFIG_OFFS_PORT_DATA, port->extra_data, PORT_PERSISTED_EXTRA_DATA_LEN);

    /* Value expression */
    if (!string_pool_write(strings_ptr, strings_offs, port->sexpr, base_ptr + CONFIG_OFFS_PORT_EXPR)) {
        DEBUG_PORT(port, "no more string space to save expression");
    }

    /* transform_write */
    if (!string_pool_write(strings_ptr, strings_offs, port->stransform_write, base_ptr + CONFIG_OFFS_PORT_TRANS_W)) {
        DEBUG_PORT(port, "no more string space to save write transform");
    }

    /* transform_read */
    if (!string_pool_write(strings_ptr, strings_offs, port->stransform_read, base_ptr + CONFIG_OFFS_PORT_TRANS_R)) {
        DEBUG_PORT(port, "no more string space to save read transform");
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

        if (!string_pool_write(strings_ptr, strings_offs, choices_str, base_ptr + CONFIG_OFFS_PORT_CHOICES)) {
            DEBUG_PORT(port, "no more string space to save port choices");
        }

        free(choices_str);
    }
    else {
        string_pool_write(strings_ptr, strings_offs, NULL, base_ptr + CONFIG_OFFS_PORT_CHOICES);
    }
}

void ports_init(uint8 *data) {
    /* Add the ports list null terminator */
    all_ports = malloc(sizeof(port_t *));
    all_ports[0] = NULL;

#ifdef HAS_GPIO
    gpio_init_ports();
#endif

#ifdef HAS_PWM
    pwm_init_ports();
#endif

#ifdef HAS_ADC
    adc_init_ports();
#endif

#ifdef HAS_VIRTUAL
    virtual_init_ports(data);
#endif

#ifdef _INIT_EXTRA_PORT_DRIVERS
    _INIT_EXTRA_PORT_DRIVERS
#endif

#ifdef _INIT_EXTERNAL_PORT_DRIVERS
    _INIT_EXTERNAL_PORT_DRIVERS
#endif

    /* Load port data */
    port_t **p = all_ports;
    while (*p) {
        DEBUG_PORT(*p, "loading data");
        port_load(*p, data);
        p++;
    }
}

void ports_save(uint8 *data, uint32 *strings_offs) {
    port_t **p = all_ports;
    while (*p) {
        DEBUG_PORT(*p, "saving data");
        port_save(*p, data, strings_offs);
        p++;
    }
}

void port_register(port_t *port) {
    if (port->slot == PORT_SLOT_AUTO) {
        if (next_extra_slot > PORT_SLOT_EXTRA_MAX) {
            DEBUG("already reached max extra port slots");
        }

        port->slot = next_extra_slot++;
        DEBUG_PORT(port, "automatically allocated extra slot %d", port->slot);
    }

    /* If a unit is present, it's normally a literal string. Make sure it's a free()-able string. */
    if (port->unit) {
        port->unit = strdup(port->unit);
    }

    all_ports = realloc(all_ports, (all_ports_count + 2) * sizeof(port_t *));
    all_ports[all_ports_count++] = port;
    all_ports[all_ports_count] = NULL;

    /* Set min/max to UNDEFINED for all attrdefs */
    if (port->attrdefs) {
        attrdef_t *a, **attrdefs = port->attrdefs;
        while ((a = *attrdefs++)) {
            if (a->min == 0 && a->max == 0) {
                a->min = a->max = UNDEFINED;
            }
        }
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

    /* Shift following ports back with 1 position */
    for (i = p; i < all_ports_count - 1; i++) {
        all_ports[i] = all_ports[i + 1];
    }

    all_ports_count--;
    all_ports = realloc(all_ports, (all_ports_count + 1) * sizeof(port_t *));
    all_ports[all_ports_count] = NULL;

    DEBUG_PORT(port, "unregistered");

    return TRUE;
}

port_t *port_find_by_id(char *id) {
    port_t **port = all_ports, *p;
    while ((p = *port++)) {
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
    if (IS_UNDEFINED(port->value) || !IS_ENABLED(port)) {
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

    port_t *p, **pp = all_ports;
    while ((p = *pp++)) {
        if (!IS_ENABLED(p)) {
            continue;
        }

        if ((p->mutual_excl_mask & (1UL << port->slot)) || (port->mutual_excl_mask & (1UL << p->slot))) {
            if (IS_ENABLED(p)) {
                DEBUG_PORT(port, "mutually exclusive with %s", p->id);
                port_disable(p);
                event_push_port_update(p);
            }
        }
    }

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

    /* Attribute getters may cache values inside port->extra_info; calling all attribute getters here ensures that this
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
