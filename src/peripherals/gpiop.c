
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
#include <gpio.h>

#include "espgoodies/common.h"
#include "espgoodies/gpio.h"

#include "common.h"
#include "peripherals.h"
#include "ports.h"

#include "peripherals/gpiop.h"


#define FLAG_OUTPUT                 0
#define FLAG_PULL_UP                1
#define FLAG_PULL_DOWN              2


ICACHE_FLASH_ATTR static double     read_value(port_t *port);
ICACHE_FLASH_ATTR static bool       write_value(port_t *port, double value);
ICACHE_FLASH_ATTR static void       configure(port_t *port); // TODO needed?

ICACHE_FLASH_ATTR static void       make_ports(peripheral_t *peripheral, port_t **ports, uint8 *ports_len);


peripheral_type_t peripheral_type_gpio = {

    .make_ports = make_ports

};

double read_value(port_t *port) {
    if (IS_PORT_WRITABLE(port)) {
        return FALSE; // TODO use buffered value in extra info or something
    }
    else {
        return gpio_read_value(port->slot); // TODO use extra info to store pin
    }
}

bool write_value(port_t *port, double value) {
    DEBUG_GPIO_PORT(port, "writing %d", !!value);
    gpio_write_value(port->slot, (int) value);  // TODO use extra info to store pin

    //set_output_value(port, value); // TODO use buffered value in extra info or something

    return TRUE;
}

void configure(port_t *port) {
    bool value;
    if (port->slot == 16) {
//        if (IS_PULL_DOWN(port)) {
//            DEBUG_GPIO_PORT(port, "pull-down enabled");
//            value = FALSE;
//        }
//        else {
//            DEBUG_GPIO_PORT(port, "pull-down disabled");
//            value = TRUE;
//        }
    }
    else {
//        if (IS_PULL_UP(port)) {
//            DEBUG_GPIO_PORT(port, "pull-up enabled");
//            value = TRUE;
//        }
//        else {
//            DEBUG_GPIO_PORT(port, "pull-up disabled");
//            value = FALSE;
//        }
    }

    if (IS_PORT_WRITABLE(port)) {
        DEBUG_GPIO_PORT(port, "output enabled");

        /* Set initial value to current GPIO value */
        value = gpio_read_value(port->slot);

        gpio_configure_output(port->slot, value);
        //set_output_value(port, value);
    }
    else {
        DEBUG_GPIO_PORT(port, "output disabled");
        gpio_configure_input(port->slot, value);
    }
}


void make_ports(peripheral_t *peripheral, port_t **ports, uint8 *ports_len) {
//    uint8 pin = PERIPHERAL_BYTE_PARAM(peripheral, 0);
//    bool output = PERIPHERAL_FLAG(peripheral, FLAG_OUTPUT);
//    bool pull_up = PERIPHERAL_FLAG(peripheral, FLAG_PULL_UP);
//    bool pull_down = PERIPHERAL_FLAG(peripheral, FLAG_PULL_DOWN);

    port_t *port = zalloc(sizeof(port_t));

//    /* Try to use slot corresponding to GPIO pin */
//    port->slot = ports_slot_busy(pin) ? ports_next_slot(/* prefer_extra = */ FALSE) : pin;

    port->type = PORT_TYPE_BOOLEAN;

    port->read_value = read_value;
    port->write_value = write_value;
    port->configure = configure;

    ports[*ports_len++] = port;
//    port_register(port);
}
