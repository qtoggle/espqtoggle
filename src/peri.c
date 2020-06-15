
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
#include "peri.h"

#include "peri/gpio.h"


static peri_class_t *all_peri_classes[] = {
    &peri_class_gpio
};

static uint8  * peri_data = NULL; /* != NULL only when changed */
static uint8    peri_count = 0;


ICACHE_FLASH_ATTR void peri_load(uint8 index, port_t **ports, uint8 *ports_len);


void peri_init(uint8 *config_data) {
    peri_data = malloc(CONFIG_PERI_SIZE * PERI_MAX_NUM);
    memcpy(peri_data, config_data + CONFIG_OFFS_PERI_BASE, CONFIG_PERI_SIZE * PERI_MAX_NUM);

    int i;
    port_t *ports[PERI_MAX_PORTS];
    uint8 ports_len;

    for (i = 0; i < PERI_MAX_NUM; i++) {
        ports_len = 0;
        peri_load(i, ports, &ports_len);
    }

    free(peri_data);
    peri_data = NULL;
}

void peri_save(uint8 *config_data) {
    if (!peri_data) {
        return; /* No changes to peripherals */
    }

    DEBUG_PERI("saving peripherals");

    memcpy(config_data + CONFIG_OFFS_PERI_BASE, peri_data, CONFIG_PERI_SIZE * PERI_MAX_NUM);
}


void peri_reset(void) {
    if (peri_data) {
        DEBUG_PERI("freeing peripheral data");
        free(peri_data);
        peri_data = NULL;
    }

    peri_count = 0;
}

void peri_register(peri_t *peri, char *port_ids[], uint8 port_ids_len) {
    if (peri_count >= PERI_MAX_NUM) {
        DEBUG_PERI("too many peripherals");
        return;
    }

    if (!peri_data) {
        DEBUG_PERI("allocating peripheral data");
        peri_data = zalloc(CONFIG_PERI_SIZE * PERI_MAX_NUM);
    }

    DEBUG_PERI("registering peripheral of class id %d at index %d", peri->class_id, peri_count);

    uint8 *data = peri_data + peri_count * CONFIG_PERI_SIZE;

    memcpy(data, &peri->class_id, 2);
    memcpy(data + 2, &peri->flags, 2);
    memcpy(data + 4, peri->raw_params, PERI_MAX_RAW_PARAMS);

    port_t *ports[PERI_MAX_PORTS];
    uint8 ports_len = 0;
    int i;

    peri_load(peri_count++, ports, &ports_len);

    /* Assign supplied IDs */
    for (i = 0; i < ports_len; i++) {
        if (i >= port_ids_len) {
            continue; /* Not enough supplied IDs */
        }

        strncpy(ports[i]->id, port_ids[i], PORT_MAX_ID_LEN + 1);
    }
}


void peri_load(uint8 index, port_t **ports, uint8 *ports_len) {
    uint8 *data = peri_data + index * CONFIG_PERI_SIZE;
    peri_t peri;
    int i;

    memcpy(&peri.class_id, data, 2);
    if (!peri.class_id || peri.class_id > PERI_MAX_CLASS_ID) {
        return; /* Peripheral not configured */
    }

    DEBUG_PERI("loading peripheral of class id %d at index %d", peri.class_id, index);

    memcpy(&peri.flags, data + 2, 2);
    memcpy(peri.raw_params, data + 4, PERI_MAX_RAW_PARAMS);

    peri_class_t *cls = all_peri_classes[peri.class_id - 1 /* Class IDs start at 1 */];

    cls->make_ports(&peri, ports, ports_len);

    for (i = 0; i < *ports_len; i++) {
        port_t *port = ports[i];

        if (port->slot >= 0 && ports_slot_busy(port->slot)) {
            port->slot = -1;
        }

        /* Automatically allocate next available slot, if not supplied */
        port->slot = ports_next_slot();
        if (!port->slot) {
            DEBUG_PERI("could not allocate slot for new port");
            free(port);
            break;
        }

        port_register(port);
    }
}
