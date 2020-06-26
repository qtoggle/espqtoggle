
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


#include <user_interface.h>
#include <mem.h>

#include "espgoodies/common.h"

#include "config.h"
#include "peripherals.h"

#include "peripherals/gpiop.h"


static peripheral_type_t *all_peripheral_types[] = {

    &peripheral_type_gpio       /* Type 1 */

};

peripheral_t  ** all_peripherals = NULL;
uint8            all_peripherals_count = 0;


ICACHE_FLASH_ATTR static void peripheral_load(peripheral_t *peripheral, uint8 *config_data);
ICACHE_FLASH_ATTR static void peripheral_save(peripheral_t *peripheral, uint8 *config_data, uint32 *strings_offs);


void peripherals_init(uint8 *config_data) {
    int i;
    uint8 *data;
    uint16 type_id;
    peripheral_t *peripheral;

    DEBUG_PERIPHERALS("initializing");

    for (i = 0; i < PERIPHERAL_MAX_NUM; i++) {
        data = config_data + CONFIG_OFFS_PERIPHERALS_BASE + i * CONFIG_PERIPHERAL_SIZE;
        memcpy(&type_id, data + PERIPHERAL_CONFIG_OFFS_TYPE_ID, 2);
        if (!type_id || type_id > PERIPHERAL_MAX_TYPE_ID) {
            break; /* Peripheral at given index is not configured; stop at first unconfigured peripheral index */
        }

        peripheral = zalloc(sizeof(peripheral_t));
        peripheral->index = i;

        peripheral_register(peripheral);
        peripheral_load(peripheral, config_data);
        peripheral_init(peripheral, /* port_ids = */ NULL, /* port_ids_len = */ 0);
    }
}

void peripherals_save(uint8 *config_data, uint32 *strings_offs) {
    DEBUG_PERIPHERALS("saving");

    /* Clear any pre-existing peripheral config data */
    memset(config_data + CONFIG_OFFS_PERIPHERALS_BASE, 0, CONFIG_PERIPHERAL_SIZE * PERIPHERAL_MAX_NUM);

    for (int i = 0; i < all_peripherals_count; i++) {
        peripheral_save(all_peripherals[i], config_data, strings_offs);
    }
}

void peripheral_register(peripheral_t *peripheral) {
    DEBUG_PERIPHERAL(peripheral, "registering");

    /* Add new peripheral to peripherals list */
    all_peripherals = realloc(all_peripherals, (all_peripherals_count + 1) * sizeof(peripheral_t *));
    all_peripherals[all_peripherals_count++] = peripheral;
}

void peripheral_unregister(peripheral_t *peripheral) {
    int i, p = -1;
    for (i = 0; i < all_peripherals_count; i++) {
        if (all_peripherals[i] == peripheral) {
            p = i;
            break;
        }
    }

    if (p == -1) {
        DEBUG_PERIPHERAL(peripheral, "not found in all peripherals list");
        return;  /* Peripheral not found */
    }

    /* Shift following peripherals back by 1 position */
    for (i = p; i < all_peripherals_count - 1; i++) {
        all_peripherals[i] = all_peripherals[i + 1];
    }

    if (all_peripherals_count > 1) {
        all_peripherals = realloc(all_peripherals, --all_peripherals_count * sizeof(peripheral_t *));
    }
    else {
        free(all_peripherals);
        all_peripherals = NULL;
        all_peripherals_count = 0;
    }

    DEBUG_PERIPHERAL(peripheral, "unregistered");
}

void peripheral_load(peripheral_t *peripheral, uint8 *config_data) {
    uint8 *data = config_data + CONFIG_OFFS_PERIPHERALS_BASE + peripheral->index * CONFIG_PERIPHERAL_SIZE;
    uint16 type_id;

    memcpy(&type_id, data + PERIPHERAL_CONFIG_OFFS_TYPE_ID, 2);
    if (!type_id || type_id > PERIPHERAL_MAX_TYPE_ID) {
        return; /* Peripheral at given index is not configured */
    }

    peripheral->type_id = type_id;

    DEBUG_PERIPHERAL(peripheral, "type %d", type_id);

    memcpy(&peripheral->flags, data + PERIPHERAL_CONFIG_OFFS_FLAGS, 2);
    memcpy(peripheral->params, data + PERIPHERAL_CONFIG_OFFS_PARAMS, PERIPHERAL_PARAMS_SIZE);
}

void peripheral_init(peripheral_t *peripheral, char *port_ids[], uint8 port_ids_len) {
    peripheral_type_t *type = all_peripheral_types[peripheral->type_id - 1 /* Type IDs start at 1 */];
    port_t *ports[PERIPHERAL_MAX_PORTS];
    uint8 ports_len = 0;

    DEBUG_PERIPHERAL(peripheral, "initializing");
    type->init(peripheral);

    DEBUG_PERIPHERAL(peripheral, "making ports");
    type->make_ports(peripheral, ports, &ports_len);

    /* Allocate slots and register new ports */
    int i;
    for (i = 0; i < ports_len; i++) {
        port_t *port = ports[i];

        if (port->slot >= 0 && ports_slot_busy(port->slot)) {
            port->slot = -1;
        }

        /* Automatically allocate next available slot, if not supplied */
        if (port->slot == -1) {
            port->slot = ports_next_slot();
            if (port->slot == -1) {
                DEBUG_PERIPHERAL(peripheral, "could not allocate slot for new port");
                free(port);
                break;
            }
        }

        port->peripheral = peripheral;

        port_register(port);
    }

    /* Assign IDs, if supplied */
    if (port_ids) {
        for (i = 0; i < ports_len; i++) {
            if (i >= port_ids_len) {
                break; /* Not enough supplied IDs */
            }

            DEBUG_PORT(ports[i], "id = \"%s\"", port_ids[i]);
            strncpy(ports[i]->id, port_ids[i], PORT_MAX_ID_LEN + 1);
        }
    }
}

void peripheral_cleanup(peripheral_t *peripheral) {
    DEBUG_PERIPHERAL(peripheral, "cleaning up");

    peripheral_type_t *type = all_peripheral_types[peripheral->type_id - 1 /* Type IDs start at 1 */];
    if (type->cleanup) {
        type->cleanup(peripheral);
    }
    else {
        /* Default cleanup routine will simply free user_data */
        if (peripheral->user_data) {
            free(peripheral->user_data);
        }
    }
}

void peripheral_save(peripheral_t *peripheral, uint8 *config_data, uint32 *strings_offs) {
    DEBUG_PERIPHERAL(peripheral, "saving");

    uint8 *data = config_data + CONFIG_OFFS_PERIPHERALS_BASE + peripheral->index * CONFIG_PERIPHERAL_SIZE;

    memcpy(data + PERIPHERAL_CONFIG_OFFS_TYPE_ID, &peripheral->type_id, 2);
    memcpy(data + PERIPHERAL_CONFIG_OFFS_FLAGS, &peripheral->flags, 2);
    memcpy(data + PERIPHERAL_CONFIG_OFFS_PARAMS, peripheral->params, PERIPHERAL_PARAMS_SIZE);
}
