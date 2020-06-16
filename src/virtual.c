
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
#include <user_interface.h>

#include "espgoodies/common.h"
#include "espgoodies/utils.h"

#include "common.h"
#include "config.h"
#include "stringpool.h"
#include "virtual.h"


#ifdef _VIRTUAL


#define SAMP_INT                        100  /* Milliseconds */ // TODO can we lower this?

#define CONFIG_CUSTOM_DATA_OFFS_MIN     0x00 /* 4 bytes */
#define RETRIES_DATA_OFFS               0x05 /* 1 bytes */


ICACHE_FLASH_ATTR static double         read_value(port_t *port);
ICACHE_FLASH_ATTR static bool           write_value(port_t *port, double value);
ICACHE_FLASH_ATTR static void           init_virtual_port(uint8 *data, char *strings_ptr, uint32 flags, uint8 rel_slot);


static double                           virtual_values[VIRTUAL_MAX_PORTS];


double read_value(port_t *port) {
    return virtual_values[port->slot - PORT_SLOT_VIRTUAL0];
}

bool write_value(port_t *port, double value) {
    virtual_values[port->slot - PORT_SLOT_VIRTUAL0] = value;

    return TRUE;
}

void init_virtual_port(uint8 *base_ptr, char *strings_ptr, uint32 flags, uint8 index) {
    port_t *port = zalloc(sizeof(port_t));

    port->slot = index + PORT_SLOT_VIRTUAL0;

    /* Virtual ports don't have an id before loading */
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

    /* Choices are stored in the strings pool */
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

        /* Add terminating NULL */
        port->choices = realloc(port->choices, sizeof(char *) * ++n);
        port->choices[n - 1] = NULL;
    }

    virtual_port_register(port);
    port_register(port);
}

void virtual_ports_init(uint8 *config_data) {
    uint8 *base_ptr;
    uint32 flags;
    char *strings_ptr = (char *) config_data + CONFIG_OFFS_STR_BASE;
    int i;

    for (i = 0; i < VIRTUAL_MAX_PORTS; i++) {
        base_ptr = config_data + CONFIG_OFFS_PORT_BASE + CONFIG_PORT_SIZE * (i + PORT_SLOT_VIRTUAL0);
        memcpy(&flags, base_ptr + CONFIG_OFFS_PORT_FLAGS, 4);

        if (flags & PORT_FLAG_VIRTUAL_ACTIVE) {
            init_virtual_port(base_ptr, strings_ptr, flags, i);
        }
    }
}

void virtual_ports_save(uint8 *config_data, uint32 *strings_offs) {
    uint8 *base_ptr;
    uint32 i, flags;
    uint8 slot;

    for (i = 0; i < VIRTUAL_MAX_PORTS; i++) {
        slot = i + PORT_SLOT_VIRTUAL0;
        base_ptr = config_data + CONFIG_OFFS_PORT_BASE + CONFIG_PORT_SIZE * slot;
        memcpy(&flags, base_ptr + CONFIG_OFFS_PORT_FLAGS, 4);
        if (ports_slot_busy(slot)) {
            flags |= PORT_FLAG_VIRTUAL_ACTIVE;
            DEBUG_VIRTUAL("setting slot %d virtual flag", slot);
        }
        else {
            flags &= ~PORT_FLAG_VIRTUAL_ACTIVE;
            DEBUG_VIRTUAL("clearing slot %d virtual flag", slot);
        }
        memcpy(base_ptr + CONFIG_OFFS_PORT_FLAGS, &flags, 4);
    }
}

int8 virtual_find_unused_slot(void) {
    /* Find next available slot */
    int i, slot;
    for (i = 0; i < VIRTUAL_MAX_PORTS; i++) {
        slot = i + PORT_SLOT_VIRTUAL0;
        if (!ports_slot_busy(slot)) {
            return slot;
        }
    }

    return -1;
}

bool virtual_port_register(port_t *port) {
    if (port->slot < 0) {
        int8 unused_slot = virtual_find_unused_slot();
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
    port->flags |= PORT_FLAG_WRITABLE;
    port->def_sampling_interval = SAMP_INT;

    port->read_value = read_value;
    port->write_value = write_value;

    return TRUE;
}

bool virtual_port_unregister(port_t *port) {
    if (port->slot < PORT_SLOT_VIRTUAL0 || port->slot > PORT_SLOT_VIRTUAL0 + VIRTUAL_MAX_PORTS - 1) {
        DEBUG_VIRTUAL("unregister: invalid slot %d", port->slot);
        return FALSE;
    }

    port->flags &= ~PORT_FLAG_VIRTUAL_ACTIVE;

    DEBUG_VIRTUAL("unregister: slot %d is now free", port->slot);

    return TRUE;
}


#endif  /* _VIRTUAL */
