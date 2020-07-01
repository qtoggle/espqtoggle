
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

#include <c_types.h>
#include <user_interface.h>

#include "espgoodies/common.h"

#include "common.h"
#include "peripherals.h"
#include "ports.h"

#include "peripherals/adc.h"


#define DEF_SLOT              17    /* Preferred ADC slot */
#define DEF_SAMPLING_INTERVAL 1000  /* Milliseconds */
#define MIN_SAMPLING_INTERVAL 0     /* Milliseconds */
#define MAX_SAMPLING_INTERVAL 60000 /* Milliseconds */


ICACHE_FLASH_ATTR static double read_value(port_t *port);
ICACHE_FLASH_ATTR static void   make_ports(peripheral_t *peripheral, port_t **ports, uint8 *ports_len);


peripheral_type_t peripheral_type_adc = {

    .make_ports = make_ports

};


double read_value(port_t *port) {
    return system_adc_read();
}

void make_ports(peripheral_t *peripheral, port_t **ports, uint8 *ports_len) {
    port_t *port = zalloc(sizeof(port_t));

    port->slot = DEF_SLOT;
    port->type = PORT_TYPE_NUMBER;
    port->min_sampling_interval = MIN_SAMPLING_INTERVAL;
    port->max_sampling_interval = MAX_SAMPLING_INTERVAL;
    port->def_sampling_interval = DEF_SAMPLING_INTERVAL;
    port->unit = "mV";
    port->min = 0;
    port->max = 1000;
    port->integer = TRUE;

    port->read_value = read_value;

    ports[(*ports_len)++] = port;
}
