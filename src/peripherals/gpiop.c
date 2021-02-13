
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
#include "espgoodies/drivers/gpio.h"

#include "common.h"
#include "peripherals.h"
#include "ports.h"

#include "peripherals/gpiop.h"


#define FLAG_NO_OUTPUT         0
#define FLAG_NO_PULL_UP        1
#define FLAG_NO_PULL_DOWN      2
#define FLAG_NO_DEF_VAL        3
#define FLAG_NO_IGNORE_DEF_VAL 4

#define PARAM_NO_PIN           0


typedef struct {

    uint8 pin;
    bool  output;
    bool  value;

} user_data_t;


static void   ICACHE_FLASH_ATTR configure(port_t *port, bool enabled);
static double ICACHE_FLASH_ATTR read_value(port_t *port);
static bool   ICACHE_FLASH_ATTR write_value(port_t *port, double value);

static void   ICACHE_FLASH_ATTR init(peripheral_t *peripheral);
static void   ICACHE_FLASH_ATTR make_ports(peripheral_t *peripheral, port_t **ports, uint8 *ports_len);


peripheral_type_t peripheral_type_gpiop = {

    .init = init,
    .make_ports = make_ports

};


void configure(port_t *port, bool enabled) {
    user_data_t *user_data = port->peripheral->user_data;

    /* If port disabled, leave GPIO as input, pulled down (probably floating) */
    if (!enabled) {
        gpio_configure_input(user_data->pin, /* pull = */ FALSE);
        return;
    }

    bool pull_up = PERIPHERAL_GET_FLAG(port->peripheral, FLAG_NO_PULL_UP);
    bool pull_down = PERIPHERAL_GET_FLAG(port->peripheral, FLAG_NO_PULL_DOWN);

    bool value;
    if (user_data->pin == 16) {
        if (pull_down) {
            DEBUG_GPIOP_PORT(port, "pull-down enabled");
            value = FALSE;
        }
        else {
            DEBUG_GPIOP_PORT(port, "pull-down disabled");
            value = TRUE;
        }
    }
    else {
        if (pull_up) {
            DEBUG_GPIOP_PORT(port, "pull-up enabled");
            value = TRUE;
        }
        else {
            DEBUG_GPIOP_PORT(port, "pull-up disabled");
            value = FALSE;
        }
    }

    if (user_data->output) {
        DEBUG_GPIOP_PORT(port, "output enabled");

        /* Set initial value to current GPIO value */
        value = gpio_read_value(user_data->pin);
        gpio_configure_output(user_data->pin, value);
        user_data->value = value;
    }
    else {
        DEBUG_GPIOP_PORT(port, "output disabled");
        gpio_configure_input(user_data->pin, value);
    }
}

double read_value(port_t *port) {
    user_data_t *user_data = port->peripheral->user_data;

    if (user_data->output) {
        return user_data->value; /* Use cached value instead of reading actual GPIO value */
    }
    else {
        return gpio_read_value(user_data->pin);
    }
}

bool write_value(port_t *port, double value) {
    user_data_t *user_data = port->peripheral->user_data;
    bool v = !!value;

    DEBUG_GPIOP_PORT(port, "writing %d", v);
    gpio_write_value(user_data->pin, v);
    user_data->value = v;

    return TRUE;
}

void init(peripheral_t *peripheral) {
    user_data_t *user_data = zalloc(sizeof(user_data_t));

    user_data->pin = PERIPHERAL_PARAM_UINT8(peripheral, PARAM_NO_PIN);
    user_data->output = PERIPHERAL_GET_FLAG(peripheral, FLAG_NO_OUTPUT);

    peripheral->user_data = user_data;
}

void make_ports(peripheral_t *peripheral, port_t **ports, uint8 *ports_len) {
    user_data_t *user_data = peripheral->user_data;
    port_t *port = port_new();

    port->slot = user_data->pin; /* Try to use slot corresponding to GPIO pin */
    port->type = PORT_TYPE_BOOLEAN;
    if (!PERIPHERAL_GET_FLAG(peripheral, FLAG_NO_IGNORE_DEF_VAL)) {
        port->last_read_value = PERIPHERAL_GET_FLAG(peripheral, FLAG_NO_DEF_VAL);
    }

    if (user_data->output) {
        port->flags |= PORT_FLAG_WRITABLE;
    }

    port->configure = configure;
    port->read_value = read_value;
    port->write_value = write_value;

    ports[(*ports_len)++] = port;
}
