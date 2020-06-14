
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
#include <ets_sys.h>
#include <user_interface.h>

#include "espgoodies/common.h"
#include "espgoodies/drivers/gpio.h"
#include "espgoodies/system.h"
#include "espgoodies/utils.h"

#include "common.h"
#include "peripherals.h"
#include "ports.h"

#include "peripherals/bl0937.h"


#define FLAG_NO_CM_SEL   0
#define PARAM_NO_CF_PIN  0
#define PARAM_NO_CF1_PIN 1
#define PARAM_NO_SEL_PIN 2

#define CURRENT_VOLTAGE_MODE_DURATION 5e5 /* Microseconds */

#define MIN_SAMP_INT 1000    /* Milliseconds */
#define DEF_SAMP_INT 5000    /* Milliseconds */
#define MAX_SAMP_INT 3600000 /* Milliseconds */


typedef struct {

    int8   cf_pin_no;
    int8   cf1_pin_no;
    int8   sel_pin_no;

    uint64 last_cf1_switch_time_us;
    uint64 last_cf1_interrupt_time_us;
    uint64 last_cf_interrupt_time_us;

    uint64 power_pulse_width_sum;
    uint32 power_pulse_count;
    uint64 total_power_pulse_count;
    uint64 current_pulse_width_sum;
    uint32 current_pulse_count;
    uint64 voltage_pulse_width_sum;
    uint32 voltage_pulse_count;

    bool   mode;
    uint32 enabled_ports_mask;
    bool   interrupt_handler_added;

} user_data_t;


static void                     handle_gpio_interrupts(uint8 gpio_no, bool value, peripheral_t *peripheral);

static double ICACHE_FLASH_ATTR read_active_power(port_t *port);
static double ICACHE_FLASH_ATTR read_energy(port_t *port);
static double ICACHE_FLASH_ATTR read_current(port_t *port);
static double ICACHE_FLASH_ATTR read_voltage(port_t *port);

static void   ICACHE_FLASH_ATTR configure(port_t *port, bool enabled);

static void   ICACHE_FLASH_ATTR init(peripheral_t *peripheral);
static void   ICACHE_FLASH_ATTR cleanup(peripheral_t *peripheral);
static void   ICACHE_FLASH_ATTR make_ports(peripheral_t *peripheral, port_t **ports, uint8 *ports_len);


peripheral_type_t peripheral_type_bl0937 = {

    .init = init,
    .cleanup = cleanup,
    .make_ports = make_ports

};


void handle_gpio_interrupts(uint8 gpio_no, bool value, peripheral_t *peripheral) {
    user_data_t *user_data = peripheral->user_data;

    /* Ignore interrupt if no port is enabled */
    if (!user_data->enabled_ports_mask) {
        return;
    }

    uint64 now_us = system_uptime_us();
    int8 cf_pin_no = user_data->cf_pin_no;
    int8 cf1_pin_no = user_data->cf1_pin_no;
    int8 sel_pin_no = user_data->sel_pin_no;

    if (gpio_no == cf_pin_no) {
        user_data->power_pulse_width_sum += now_us - user_data->last_cf_interrupt_time_us;
        user_data->power_pulse_count++;
        user_data->total_power_pulse_count++;
        user_data->last_cf_interrupt_time_us = now_us;

        return;
    }

    if (gpio_no == cf1_pin_no) {
        uint32 pulse_width = now_us - user_data->last_cf1_interrupt_time_us;

        if (user_data->mode == PERIPHERAL_GET_FLAG(peripheral, FLAG_NO_CM_SEL)) {
            user_data->current_pulse_width_sum += pulse_width;
            user_data->current_pulse_count++;
        }
        else { /* Assuming voltage mode */
            user_data->voltage_pulse_width_sum += pulse_width;
            user_data->voltage_pulse_count++;
        }

        user_data->last_cf1_interrupt_time_us = now_us;

        /* Switch from voltage to current mode, if enough time has passed in currently selected mode */
        if ((now_us - user_data->last_cf1_switch_time_us) > CURRENT_VOLTAGE_MODE_DURATION) {
            user_data->last_cf1_switch_time_us = now_us;
            user_data->mode = 1 - user_data->mode;

            if (sel_pin_no >= 0) {
                gpio_write_value(sel_pin_no, user_data->mode);
            }
        }
    }
}

double read_active_power(port_t *port) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;

    double power = 0;
    if (user_data->power_pulse_count && user_data->power_pulse_width_sum) {
        power = 1e6F * user_data->power_pulse_count / user_data->power_pulse_width_sum;
    }

    DEBUG_BL0937(peripheral, "power = %s", dtostr(power, -1));

    user_data->power_pulse_width_sum = 0;
    user_data->power_pulse_count = 0;

    return power;
}

double read_energy(port_t *port) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;

    double energy = user_data->total_power_pulse_count;
    DEBUG_BL0937(peripheral, "energy = %s", dtostr(energy, -1));

    return energy;
}

double read_current(port_t *port) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;

    double current = 0;
    if (user_data->current_pulse_count && user_data->current_pulse_width_sum) {
        current = 1e6F * user_data->current_pulse_count / user_data->current_pulse_width_sum;
    }

    DEBUG_BL0937(peripheral, "current = %s", dtostr(current, -1));

    user_data->current_pulse_width_sum = 0;
    user_data->current_pulse_count = 0;

    return current;
}

double read_voltage(port_t *port) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;

    double voltage = 0;
    if (user_data->voltage_pulse_count && user_data->voltage_pulse_width_sum) {
        voltage = 1e6F * user_data->voltage_pulse_count / user_data->voltage_pulse_width_sum;
    }

    DEBUG_BL0937(peripheral, "voltage = %s", dtostr(voltage, -1));

    user_data->voltage_pulse_width_sum = 0;
    user_data->voltage_pulse_count = 0;

    return voltage;
}

void configure(port_t *port, bool enabled) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;
    uint32 prev_enabled_ports_mask = user_data->enabled_ports_mask;

    if (enabled) {
        user_data->enabled_ports_mask |= BIT(port->slot);
    }
    else {
        user_data->enabled_ports_mask &= ~BIT(port->slot);
    }

    if (prev_enabled_ports_mask && !user_data->enabled_ports_mask) {
        DEBUG_BL0937(peripheral, "all ports disabled, disabling peripheral");
    }
    else if (!prev_enabled_ports_mask && user_data->enabled_ports_mask) {
        DEBUG_BL0937(peripheral, "first port enabled, enabling peripheral");

        if (!user_data->interrupt_handler_added) {
            user_data->interrupt_handler_added = TRUE;

            if (user_data->cf_pin_no >= 0) {
                gpio_interrupt_handler_add(
                    user_data->cf_pin_no,
                    GPIO_INTERRUPT_FALLING_EDGE,
                    (gpio_interrupt_handler_t) handle_gpio_interrupts,
                    peripheral
                );
            }

            if (user_data->cf1_pin_no >= 0) {
                gpio_interrupt_handler_add(
                    user_data->cf1_pin_no,
                    GPIO_INTERRUPT_FALLING_EDGE,
                    (gpio_interrupt_handler_t) handle_gpio_interrupts,
                    peripheral
                );
            }

            user_data->mode = PERIPHERAL_GET_FLAG(peripheral, FLAG_NO_CM_SEL);

            int8 sel_pin_no = user_data->sel_pin_no;
            if (sel_pin_no >= 0) {
                gpio_configure_output(sel_pin_no, /* initial = */ user_data->mode);
            }
        }
    }
}

void init(peripheral_t *peripheral) {
    user_data_t *user_data = zalloc(sizeof(user_data_t));

    user_data->cf_pin_no = PERIPHERAL_PARAM_SINT8(peripheral, PARAM_NO_CF_PIN);
    user_data->cf1_pin_no = PERIPHERAL_PARAM_SINT8(peripheral, PARAM_NO_CF1_PIN);
    user_data->sel_pin_no = PERIPHERAL_PARAM_SINT8(peripheral, PARAM_NO_SEL_PIN);

    peripheral->user_data = user_data;

    DEBUG_BL0937(peripheral, "using CF GPIO %d", user_data->cf_pin_no);
    DEBUG_BL0937(peripheral, "using CF1 GPIO %d", user_data->cf1_pin_no);
    DEBUG_BL0937(peripheral, "using SEL GPIO %d", user_data->sel_pin_no);
    DEBUG_BL0937(peripheral, "using SEL current mode %d", PERIPHERAL_GET_FLAG(peripheral, FLAG_NO_CM_SEL));
}

void cleanup(peripheral_t *peripheral) {
    user_data_t *user_data = peripheral->user_data;

    if (user_data->interrupt_handler_added) {
        gpio_interrupt_handler_remove(user_data->cf_pin_no, (gpio_interrupt_handler_t) handle_gpio_interrupts);
        gpio_interrupt_handler_remove(user_data->cf1_pin_no, (gpio_interrupt_handler_t) handle_gpio_interrupts);
    }
}

void make_ports(peripheral_t *peripheral, port_t **ports, uint8 *ports_len) {
    user_data_t *user_data = peripheral->user_data;

    port_t *active_power_port = port_create();
    active_power_port->slot = -1;
    active_power_port->type = PORT_TYPE_NUMBER;
    active_power_port->min_sampling_interval = MIN_SAMP_INT;
    active_power_port->max_sampling_interval = MAX_SAMP_INT;
    active_power_port->def_sampling_interval = DEF_SAMP_INT;
    active_power_port->read_value = read_active_power;
    active_power_port->configure = configure;
    ports[(*ports_len)++] = active_power_port;

    port_t *energy_port = port_create();
    energy_port->slot = -1;
    energy_port->type = PORT_TYPE_NUMBER;
    energy_port->min_sampling_interval = MIN_SAMP_INT;
    energy_port->max_sampling_interval = MAX_SAMP_INT;
    energy_port->def_sampling_interval = DEF_SAMP_INT;
    energy_port->read_value = read_energy;
    energy_port->configure = configure;
    ports[(*ports_len)++] = energy_port;

    if (user_data->cf1_pin_no >= 0) {
        port_t *current_port = port_create();
        current_port->slot = -1;
        current_port->type = PORT_TYPE_NUMBER;
        current_port->min_sampling_interval = MIN_SAMP_INT;
        current_port->max_sampling_interval = MAX_SAMP_INT;
        current_port->def_sampling_interval = DEF_SAMP_INT;
        current_port->read_value = read_current;
        current_port->configure = configure;
        ports[(*ports_len)++] = current_port;

        port_t *voltage_port = port_create();
        voltage_port->slot = -1;
        voltage_port->type = PORT_TYPE_NUMBER;
        voltage_port->min_sampling_interval = MIN_SAMP_INT;
        voltage_port->max_sampling_interval = MAX_SAMP_INT;
        voltage_port->def_sampling_interval = DEF_SAMP_INT;
        voltage_port->read_value = read_voltage;
        voltage_port->configure = configure;
        ports[(*ports_len)++] = voltage_port;
    }
}
