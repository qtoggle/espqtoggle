
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

#include "espgoodies/common.h"
#include "espgoodies/drivers/pwm.h"

#include "common.h"
#include "peripherals.h"
#include "ports.h"

#include "peripherals/pwmp.h"


#define PARAM_NO_PIN 0

#define MIN_FREQ     1     /* Hz */
#define MAX_FREQ     50000 /* Hz */
#define DEF_FREQ     1000  /* Hz */
#define DEF_DUTY     50    /* % */


typedef struct {

    uint8 pin;
    uint8 duty;
    bool  owns_freq_port;

} user_data_t;


static void   ICACHE_FLASH_ATTR configure_duty(port_t *port, bool enabled);
static double ICACHE_FLASH_ATTR read_value_duty(port_t *port);
static bool   ICACHE_FLASH_ATTR write_value_duty(port_t *port, double value);

static void   ICACHE_FLASH_ATTR configure_freq(port_t *port, bool enabled);
static double ICACHE_FLASH_ATTR read_value_freq(port_t *port);
static bool   ICACHE_FLASH_ATTR write_value_freq(port_t *port, double value);

static void   ICACHE_FLASH_ATTR init(peripheral_t *peripheral);
static void   ICACHE_FLASH_ATTR cleanup(peripheral_t *peripheral);
static void   ICACHE_FLASH_ATTR make_ports(peripheral_t *peripheral, port_t **ports, uint8 *ports_len);


peripheral_type_t peripheral_type_pwmp = {

    .init = init,
    .cleanup = cleanup,
    .make_ports = make_ports

};


static bool   freq_port_created = FALSE;
static uint32 curr_freq = DEF_FREQ;
static uint32 used_gpio_mask = 0;


void configure_duty(port_t *port, bool enabled) {
    user_data_t *user_data = port->peripheral->user_data;

    if (enabled) {
        pwm_set_duty(user_data->pin, user_data->duty);
    }
    else {
        pwm_set_duty(user_data->pin, 0);
    }
}

double read_value_duty(port_t *port) {
    user_data_t *user_data = port->peripheral->user_data;

    return user_data->duty;
}

bool write_value_duty(port_t *port, double value) {
    user_data_t *user_data = port->peripheral->user_data;

    DEBUG_PWMP_PORT(port, "setting duty to %d%%", (int) value);
    pwm_set_duty(user_data->pin, value);

    return TRUE;
}

void configure_freq(port_t *port, bool enabled) {
    if (enabled) {
        pwm_set_freq(curr_freq);
        pwm_enable();
    }
    else {
        pwm_disable();
    }
}

double read_value_freq(port_t *port) {
    return curr_freq;
}

bool write_value_freq(port_t *port, double value) {
    curr_freq = value;
    pwm_set_freq(curr_freq);

    return TRUE;
}

void init(peripheral_t *peripheral) {
    user_data_t *user_data = zalloc(sizeof(user_data_t));

    user_data->pin = PERIPHERAL_PARAM_UINT8(peripheral, PARAM_NO_PIN);
    user_data->duty = DEF_DUTY;

    peripheral->user_data = user_data;

    used_gpio_mask |= BIT(user_data->pin);
    pwm_setup(used_gpio_mask);
}

void cleanup(peripheral_t *peripheral) {
    user_data_t *user_data = peripheral->user_data;

    /* If this peripheral owns the frequency port, invalidate it */
    if (user_data->owns_freq_port) {
        DEBUG_PWMP(peripheral, "invalidating freq port");
        freq_port_created = FALSE;
    }

    used_gpio_mask &= ~BIT(user_data->pin);
    pwm_setup(used_gpio_mask);
}

void make_ports(peripheral_t *peripheral, port_t **ports, uint8 *ports_len) {
    user_data_t *user_data = peripheral->user_data;

    /* Ensure we also have a frequency port */
    if (!freq_port_created) {
        freq_port_created = TRUE;
        user_data->owns_freq_port = TRUE;

        port_t *freq_port = port_create();

        freq_port->slot = -1;
        freq_port->type = PORT_TYPE_NUMBER;
        freq_port->flags |= PORT_FLAG_WRITABLE;
        freq_port->min = MIN_FREQ;
        freq_port->max = MAX_FREQ;
        freq_port->integer = TRUE;
        freq_port->unit = "Hz";

        freq_port->configure = configure_freq;
        freq_port->read_value = read_value_freq;
        freq_port->write_value = write_value_freq;

        ports[(*ports_len)++] = freq_port;
    }

    port_t *port = port_create();

    port->slot = user_data->pin; /* Try to use slot corresponding to GPIO pin */
    port->type = PORT_TYPE_NUMBER;
    port->flags |= PORT_FLAG_WRITABLE;
    port->min = 0;
    port->max = 100;
    port->integer = TRUE;
    port->unit = "%";

    port->configure = configure_duty;
    port->read_value = read_value_duty;
    port->write_value = write_value_duty;

    ports[(*ports_len)++] = port;
}
