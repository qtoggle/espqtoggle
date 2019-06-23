
/*
 * Copyright (c) Calin Crisan
 * This file is part of espQToggle.
 *
 * espQToggle is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>. 
 */

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <c_types.h>
#include <mem.h>
#include <user_interface.h>

#include "espgoodies/common.h"
#include "espgoodies/utils.h"

#include "config.h"
#include "stringpool.h"
#include "ports/virtual.h"


#ifdef HAS_VIRTUAL


#define SAMP_INT                        100  /* milliseconds */

#define CONFIG_CUSTOM_DATA_OFFS_MIN     0x00 /* 4 bytes */
#define RETRIES_DATA_OFFS               0x05 /* 1 bytes */


ICACHE_FLASH_ATTR static double         read_value(port_t *port);
ICACHE_FLASH_ATTR static bool           write_value(port_t *port, double value);
ICACHE_FLASH_ATTR static void           init_virtual_port(uint8 *data, char *strings_ptr, uint32 flags, uint8 rel_slot);


static double                           virtual_values[VIRTUAL_MAX_PORTS];
static bool                             virtual_port_in_use[VIRTUAL_MAX_PORTS];


double read_value(port_t *port) {
    return virtual_values[port->slot - VIRTUAL0_SLOT];
}

bool write_value(port_t *port, double value) {
    virtual_values[port->slot - VIRTUAL0_SLOT] = value;

    return TRUE;
}

void init_virtual_port(uint8 *base_ptr, char *strings_ptr, uint32 flags, uint8 index) {
    port_t *port = zalloc(sizeof(port_t));

    port->slot = index + VIRTUAL0_SLOT;

    /* virtual ports don't have an id before loading */
    snprintf(port->id, PORT_MAX_ID_LEN + 1, "virtual%d", index);

    char *id = string_pool_read(strings_ptr, base_ptr + CONFIG_OFFS_PORT_ID);
    if (id) {
        strncpy(port->id, id, PORT_MAX_ID_LEN + 1);
    }

    port->type = flags & PORT_FLAG_VIRTUAL_TYPE ? PORT_TYPE_NUMBER : PORT_TYPE_BOOLEAN;
    port->integer = !!(flags & PORT_FLAG_VIRTUAL_INTEGER);

    memcpy(&port->min, base_ptr + CONFIG_OFFS_PORT_MIN, sizeof(double));
    memcpy(&port->max, base_ptr + CONFIG_OFFS_PORT_MAX, sizeof(double));
    memcpy(&port->step, base_ptr + CONFIG_OFFS_PORT_STEP, sizeof(double));

    DEBUG_PORT(port, "type = %s", port->type == PORT_TYPE_NUMBER ? "number" : "boolean");
    DEBUG_PORT(port, "integer = %s", port->integer ? "true" : "false");
    DEBUG_PORT(port, "min = %s", IS_UNDEFINED(port->min) ? "unset" : dtostr(port->min, -1));
    DEBUG_PORT(port, "max = %s", IS_UNDEFINED(port->max) ? "unset" : dtostr(port->max, -1));
    DEBUG_PORT(port, "step = %s", IS_UNDEFINED(port->step) ? "unset" : dtostr(port->step, -1));

    /* choices are stored in the strings pool */
    char *choices_str = string_pool_read(strings_ptr, base_ptr + CONFIG_OFFS_PORT_CHOICES);
    if (choices_str && port->type == PORT_TYPE_NUMBER) {
        char *choice;
        int n = 0;

        choice = strtok(choices_str, "|");
        while (choice) {
            DEBUG_PORT(port, "choice: %s", choice);
            port->choices = realloc(port->choices, sizeof(char *) * ++n);
            port->choices[n - 1] = strdup(choice);
            choice = strtok(NULL, "|");
        }

        /* add terminating NULL */
        port->choices = realloc(port->choices, sizeof(char *) * ++n);
        port->choices[n - 1] = NULL;
    }

    virtual_port_register(port);

    virtual_port_in_use[index] = TRUE;
}

void virtual_init_ports(uint8 *data) {
    uint8 *base_ptr;
    uint32 flags;
    char *strings_ptr = (char *) data + CONFIG_OFFS_STR_BASE;
    int i;

    for (i = 0; i < VIRTUAL_MAX_PORTS; i++) {
        base_ptr = data + CONFIG_OFFS_PORT_BASE + CONFIG_PORT_SIZE * (i + VIRTUAL0_SLOT);
        memcpy(&flags, base_ptr + CONFIG_OFFS_PORT_FLAGS, 4);

        if (flags & PORT_FLAG_VIRTUAL_ACTIVE) {
            init_virtual_port(base_ptr, strings_ptr, flags, i);
        }
    }
}

int8 virtual_find_unused_slot(bool occupy) {
    /* find next available slot */
    int i;
    for (i = 0; i < VIRTUAL_MAX_PORTS; i++) {
        if (!virtual_port_in_use[i]) {
            if (occupy) {
                virtual_port_in_use[i] = TRUE;
            }

            return i + VIRTUAL0_SLOT;
        }
    }

    return -1;
}

bool virtual_port_register(port_t *port) {
    if (port->slot < 0) {
        int8 unused_slot = virtual_find_unused_slot(/* occupy = */ TRUE);
        if (unused_slot < 0) {
            DEBUG_VIRTUAL("register: no more free slots");
            return FALSE;
        }

        port->slot = unused_slot;
        DEBUG_VIRTUAL("register: found unused slot %d", unused_slot);
    }

    if (port->type == PORT_TYPE_NUMBER) {
        port->flags |= PORT_FLAG_VIRTUAL_TYPE;
    }

    port->flags |= PORT_FLAG_VIRTUAL_ACTIVE;
    port->flags |= PORT_FLAG_OUTPUT;
    port->flags |= PORT_FLAG_ENABLED;
    port->def_sampling_interval = SAMP_INT;

    port->read_value = read_value;
    port->write_value = write_value;

    port_register(port);

    DEBUG_PORT(port, "using slot %d", port->slot);

    return TRUE;
}

bool virtual_port_unregister(port_t *port) {
    if (port->slot < VIRTUAL0_SLOT || port->slot > VIRTUAL0_SLOT + VIRTUAL_MAX_PORTS - 1) {
        DEBUG_VIRTUAL("unregister: invalid slot %d", port->slot);
        return FALSE;
    }

    virtual_port_in_use[port->slot - VIRTUAL0_SLOT] = FALSE;
    port->flags &= ~PORT_FLAG_VIRTUAL_ACTIVE;

    if (!port_unregister(port)) {
        return FALSE;
    }

    DEBUG_VIRTUAL("unregister: slot %d is now free", port->slot);

    return TRUE;
}


#endif  /* HAS_VIRTUAL */
