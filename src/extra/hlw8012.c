
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
 * Based on https://github.com/MacWyznawca/HLW8012_BL0937_ESP.
 */

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <mem.h>
#include <user_interface.h>
#include <eagle_soc.h>
#include <gpio.h>

#include "espgoodies/common.h"
#include "espgoodies/gpio.h"
#include "espgoodies/system.h"
#include "espgoodies/utils.h"

#include "api.h"
#include "apiutils.h"
#include "common.h"
#include "drivers/uart.h"
#include "ports.h"
#include "extra/hlw8012.h"


#ifdef HAS_HLW8012

#define MIN_SAMP_INT                    1000    /* Milliseconds */
#define DEF_SAMP_INT                    5000    /* Milliseconds */
#define MAX_SAMP_INT                    3600000 /* Milliseconds */

/* Maximum pulse with, in microseconds. Longer pulse widths will result in a reset to 0. Higher values allow for a
 * better precision but reduce sampling rate. Values below 0.5s are not recommended since current and voltage output
 * won't stabilize. */
#define PULSE_TIMEOUT                   2000000L

#define CF_GPIO_CONFIG_OFFS             0x00 /* 1 byte */
#define CF1_GPIO_CONFIG_OFFS            0x01 /* 1 byte */
#define SEL_GPIO_CONFIG_OFFS            0x02 /* 1 byte */

#define FLAG_CURRENT_MODE_SEL           0x01000000


typedef struct {

    int8                                cf_gpio;
    int8                                cf1_gpio;
    int8                                sel_gpio;

    double                              last_voltage;
    double                              last_current;
    double                              last_active_power;
    double                              last_energy;

    uint64                              last_read_time; /* Milliseconds */
    uint64                              last_cf1_interrupt_time; /* Microseconds */
    uint64                              last_cf_interrupt_time; /* Microseconds */
    uint64                              first_cf1_interrupt_time; /* Microseconds */

    uint32                              power_pulse_width;
    uint64                              power_pulse_count;
    uint32                              current_pulse_width;
    uint32                              voltage_pulse_width;

    uint8                               mode;

} extra_info_t;


#define get_cf_gpio(p)                  (((extra_info_t *) (p)->extra_info)->cf_gpio)
#define set_cf_gpio(p, v)               {((extra_info_t *) (p)->extra_info)->cf_gpio = v;}

#define get_cf1_gpio(p)                 (((extra_info_t *) (p)->extra_info)->cf1_gpio)
#define set_cf1_gpio(p, v)              {((extra_info_t *) (p)->extra_info)->cf1_gpio = v;}

#define get_sel_gpio(p)                 (((extra_info_t *) (p)->extra_info)->sel_gpio)
#define set_sel_gpio(p, v)              {((extra_info_t *) (p)->extra_info)->sel_gpio = v;}

#define get_current_mode_sel(p)         (!!((p)->flags & FLAG_CURRENT_MODE_SEL))
#define set_current_mode_sel(p, v)      {if (v) (p)->flags |= FLAG_CURRENT_MODE_SEL; \
                                         else (p)->flags &= ~FLAG_CURRENT_MODE_SEL;}

ICACHE_FLASH_ATTR static void           configure(port_t *port);

#ifdef HAS_HLW8012_VOLTAGE
ICACHE_FLASH_ATTR static double         read_voltage(port_t *port);
#endif

#ifdef HAS_HLW8012_CURRENT
ICACHE_FLASH_ATTR static double         read_current(port_t *port);
#endif

#ifdef HAS_HLW8012_ACT_POW
ICACHE_FLASH_ATTR static double         read_act_pow(port_t *port);
#endif

#ifdef HAS_HLW8012_ENERGY
ICACHE_FLASH_ATTR static double         read_energy(port_t *port);
#endif

ICACHE_FLASH_ATTR static int8           attr_get_cf_gpio(port_t *port, attrdef_t *attrdef);
ICACHE_FLASH_ATTR static void           attr_set_cf_gpio(port_t *port, attrdef_t *attrdef, int index);

ICACHE_FLASH_ATTR static int8           attr_get_cf1_gpio(port_t *port, attrdef_t *attrdef);
ICACHE_FLASH_ATTR static void           attr_set_cf1_gpio(port_t *port, attrdef_t *attrdef, int index);

ICACHE_FLASH_ATTR static int8           attr_get_sel_gpio(port_t *port, attrdef_t *attrdef);
ICACHE_FLASH_ATTR static void           attr_set_sel_gpio(port_t *port, attrdef_t *attrdef, int index);

ICACHE_FLASH_ATTR static bool           attr_get_current_mode_sel(port_t *port, attrdef_t *attrdef);
ICACHE_FLASH_ATTR static void           attr_set_current_mode_sel(port_t *port, attrdef_t *attrdef, bool value);

ICACHE_FLASH_ATTR static bool           read_status_if_needed(port_t *port);
ICACHE_FLASH_ATTR static bool           read_status(port_t *port);

static void                             gpio_interrupt_handler(void *arg);
ICACHE_FLASH_ATTR  static void          check_cf1_toggle_mode(port_t *port);


static attrdef_t cf_gpio_attrdef = {

    .name = "cf_gpio",
    .display_name = "CF GPIO Number",
    .description = "The GPIO where the CF pin is attached.",
    .type = ATTR_TYPE_NUMBER,
    .choices = all_gpio_none_choices,
    .modifiable = TRUE,
    .set = attr_set_cf_gpio,
    .get = attr_get_cf_gpio

};

static attrdef_t cf1_gpio_attrdef = {

    .name = "cf1_gpio",
    .display_name = "CF1 GPIO Number",
    .description = "The GPIO where the CF1 pin is attached.",
    .type = ATTR_TYPE_NUMBER,
    .choices = all_gpio_none_choices,
    .modifiable = TRUE,
    .set = attr_set_cf1_gpio,
    .get = attr_get_cf1_gpio

};

static attrdef_t sel_gpio_attrdef = {

    .name = "sel_gpio",
    .display_name = "SEL GPIO Number",
    .description = "The GPIO where the SEL pin is attached.",
    .type = ATTR_TYPE_NUMBER,
    .choices = all_gpio_none_choices,
    .modifiable = TRUE,
    .set = attr_set_sel_gpio,
    .get = attr_get_sel_gpio

};

static attrdef_t current_mode_sel_attrdef = {

    .name = "current_mode_sel",
    .display_name = "Current Mode SEL Level",
    .description = "Indicates the SEL pin level for current mode.",
    .type = ATTR_TYPE_BOOLEAN,
    .modifiable = TRUE,
    .integer = TRUE,
    .set = attr_set_current_mode_sel,
    .get = attr_get_current_mode_sel

};


static attrdef_t *attrdefs[] = {

    &cf_gpio_attrdef,
    &cf1_gpio_attrdef,
    &sel_gpio_attrdef,
    &current_mode_sel_attrdef,
    NULL

};


static extra_info_t hlw8012_extra_info;


#ifdef HAS_HLW8012_VOLTAGE

static port_t _hlw8012_voltage = {

    .slot = PORT_SLOT_AUTO,

    .id = HLW8012_VOLTAGE_ID,
    .type = PORT_TYPE_NUMBER,
    .min = UNDEFINED,
    .max = UNDEFINED,
    .step = UNDEFINED,

    .extra_info = &hlw8012_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_voltage

};

port_t *hlw8012_voltage = &_hlw8012_voltage;

#endif

#ifdef HAS_HLW8012_CURRENT

static port_t _hlw8012_current = {

    .slot = PORT_SLOT_AUTO,

    .id = HLW8012_CURRENT_ID,
    .type = PORT_TYPE_NUMBER,
    .min = UNDEFINED,
    .max = UNDEFINED,
    .step = UNDEFINED,

    .extra_info = &hlw8012_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_current

};

port_t *hlw8012_current = &_hlw8012_current;

#endif

#ifdef HAS_HLW8012_ACT_POW

static port_t _hlw8012_act_pow = {

    .slot = PORT_SLOT_AUTO,

    .id = HLW8012_ACT_POW_ID,
    .type = PORT_TYPE_NUMBER,
    .min = UNDEFINED,
    .max = UNDEFINED,
    .step = UNDEFINED,

    .extra_info = &hlw8012_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_act_pow,
    .configure = configure,

    .attrdefs = attrdefs

};

port_t *hlw8012_act_pow = &_hlw8012_act_pow;

#endif

#ifdef HAS_HLW8012_ENERGY

static port_t _hlw8012_energy = {

    .slot = PORT_SLOT_AUTO,

    .id = HLW8012_ENERGY_ID,
    .type = PORT_TYPE_NUMBER,
    .min = UNDEFINED,
    .max = UNDEFINED,
    .step = UNDEFINED,

    .extra_info = &hlw8012_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_energy,

};

port_t *hlw8012_energy = &_hlw8012_energy;

#endif


void configure(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    ETS_GPIO_INTR_DISABLE();
    ETS_GPIO_INTR_ATTACH(gpio_interrupt_handler, port);

    int8 cf_gpio = get_cf_gpio(port);
    if (cf_gpio >= 0) {
        gpio_configure_input(cf_gpio, /* pull = */ TRUE);
        gpio_register_set(GPIO_PIN_ADDR(cf_gpio),
                          GPIO_PIN_INT_TYPE_SET(GPIO_PIN_INTR_DISABLE) |
                          GPIO_PIN_PAD_DRIVER_SET(GPIO_PAD_DRIVER_DISABLE) |
                          GPIO_PIN_SOURCE_SET(GPIO_AS_PIN_SOURCE));
        GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, BIT(cf_gpio));
        gpio_pin_intr_state_set(cf_gpio, GPIO_PIN_INTR_NEGEDGE);
    }

    int8 cf1_gpio = get_cf1_gpio(port);
    if (cf1_gpio >= 0) {
        gpio_configure_input(cf1_gpio, /* pull = */ TRUE);
        gpio_register_set(GPIO_PIN_ADDR(cf1_gpio),
                          GPIO_PIN_INT_TYPE_SET(GPIO_PIN_INTR_DISABLE) |
                          GPIO_PIN_PAD_DRIVER_SET(GPIO_PAD_DRIVER_DISABLE) |
                          GPIO_PIN_SOURCE_SET(GPIO_AS_PIN_SOURCE));
        GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, BIT(cf1_gpio));
        gpio_pin_intr_state_set(GPIO_ID_PIN(cf1_gpio), GPIO_PIN_INTR_NEGEDGE);
    }

    extra_info->mode = get_current_mode_sel(port);

    if (get_sel_gpio(port) >= 0) {
        gpio_configure_output(get_sel_gpio(port), /* initial = */ extra_info->mode);
    }

    extra_info->last_voltage = UNDEFINED;
    extra_info->last_current = UNDEFINED;
    extra_info->last_active_power = UNDEFINED;
    extra_info->last_energy = UNDEFINED;

    ETS_GPIO_INTR_ENABLE();
}

#ifdef HAS_HLW8012_VOLTAGE
double read_voltage(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    if (!read_status_if_needed(port)) {
        return UNDEFINED;
    }

    return extra_info->last_voltage;
}
#endif

#ifdef HAS_HLW8012_CURRENT
double read_current(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    if (!read_status_if_needed(port)) {
        return UNDEFINED;
    }

    return extra_info->last_current;
}
#endif

#ifdef HAS_HLW8012_ACT_POW
double read_act_pow(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    if (!read_status_if_needed(port)) {
        return UNDEFINED;
    }

    return extra_info->last_active_power;
}
#endif

#ifdef HAS_HLW8012_ENERGY
double read_energy(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    if (!read_status_if_needed(port)) {
        return UNDEFINED;
    }

    return extra_info->last_energy;
}
#endif


int8 attr_get_cf_gpio(port_t *port, attrdef_t *attrdef) {
    int8 value;

    /* Read from persisted data */
    memcpy(&value, port->extra_data + CF_GPIO_CONFIG_OFFS, 1);

    /* Update cached value */
    set_cf_gpio(port, get_choice_value_num(attrdef->choices[value]));

    return value;
}

void attr_set_cf_gpio(port_t *port, attrdef_t *attrdef, int index) {
    int8 value = index;

    /* Update cached value */
    set_cf_gpio(port, get_choice_value_num(attrdef->choices[value]));

    /* Write to persisted data */
    memcpy(port->extra_data + CF_GPIO_CONFIG_OFFS, &value, 1);
}

int8 attr_get_cf1_gpio(port_t *port, attrdef_t *attrdef) {
    int8 value;

    /* Read from persisted data */
    memcpy(&value, port->extra_data + CF1_GPIO_CONFIG_OFFS, 1);

    /* Update cached value */
    set_cf1_gpio(port, get_choice_value_num(attrdef->choices[value]));

    return value;
}

void attr_set_cf1_gpio(port_t *port, attrdef_t *attrdef, int index) {
    int8 value = index;

    /* Update cached value */
    set_cf1_gpio(port, get_choice_value_num(attrdef->choices[value]));

    /* Write to persisted data */
    memcpy(port->extra_data + CF1_GPIO_CONFIG_OFFS, &value, 1);
}

int8 attr_get_sel_gpio(port_t *port, attrdef_t *attrdef) {
    int8 value;

    /* Read from persisted data */
    memcpy(&value, port->extra_data + SEL_GPIO_CONFIG_OFFS, 1);

    /* Update cached value */
    set_sel_gpio(port, get_choice_value_num(attrdef->choices[value]));

    return value;
}

void attr_set_sel_gpio(port_t *port, attrdef_t *attrdef, int index) {
    int8 value = index;

    /* Update cached value */
    set_sel_gpio(port, get_choice_value_num(attrdef->choices[value]));

    /* Write to persisted data */
    memcpy(port->extra_data + SEL_GPIO_CONFIG_OFFS, &value, 1);
}

void attr_set_current_mode_sel(port_t *port, attrdef_t *attrdef, bool value) {
    set_current_mode_sel(port, value);
}

bool attr_get_current_mode_sel(port_t *port, attrdef_t *attrdef) {
    return get_current_mode_sel(port);
}

bool read_status_if_needed(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    uint64 now = system_uptime_ms();
    uint64 delta = now - extra_info->last_read_time;
    if (delta >= port->sampling_interval - 10) { /* Allow 10 milliseconds of tolerance */
        DEBUG_HLW8012("status needs new reading");
        if (get_cf1_gpio(port) >= 0) {
            check_cf1_toggle_mode(port);
        }

        /* Update last read time */
        extra_info->last_read_time = system_uptime_ms();

        if (read_status(port)) {
            return TRUE;
        }

        DEBUG_HLW8012("status reading failed");

        /* In case of error, cached status can be used within up to twice the sampling interval. */
        if (delta > port->sampling_interval * 2) {
            return FALSE;
        }
    }

    return TRUE;
}

bool read_status(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    extra_info->last_active_power = (extra_info->power_pulse_width > 0) ? 1e6F / extra_info->power_pulse_width : 0;
    DEBUG_HLW8012("read active power = %s", dtostr(extra_info->last_active_power, -1));

    extra_info->last_energy = extra_info->power_pulse_count;
    DEBUG_HLW8012("read energy = %s", dtostr(extra_info->last_energy, -1));

    extra_info->last_voltage = (extra_info->voltage_pulse_width > 0) ? 1e6F / extra_info->voltage_pulse_width : 0;
    DEBUG_HLW8012("read voltage = %s", dtostr(extra_info->last_voltage, -1));

    if (extra_info->last_active_power == 0) {
        /* Active power measurement being a bit more sensitive than current, make sure we don't report current when
         * power is zero */
        extra_info->last_current = 0;
    }
    else {
        extra_info->last_current = (extra_info->current_pulse_width > 0) ? 1e6F / extra_info->current_pulse_width : 0;
    }
    DEBUG_HLW8012("read current = %s", dtostr(extra_info->last_current, -1));

    extra_info->power_pulse_width = 0;
    extra_info->voltage_pulse_width = 0;
    extra_info->current_pulse_width = 0;

    return TRUE;
}

void gpio_interrupt_handler(void *arg) {
    port_t *port = arg;
    extra_info_t *extra_info = port->extra_info;

    uint64 now = system_uptime_us();
    int8 cf_gpio = get_cf_gpio(port);
    int8 cf1_gpio = get_cf1_gpio(port);
    uint32 gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);

    if ((cf_gpio >= 0) && (gpio_status & BIT(cf_gpio))) {
        extra_info->power_pulse_width = now - extra_info->last_cf_interrupt_time;
        extra_info->power_pulse_count++;
        extra_info->last_cf_interrupt_time = now;

        /* Disable interrupt */
        gpio_pin_intr_state_set(GPIO_ID_PIN(cf_gpio), GPIO_PIN_INTR_DISABLE);

        /* Clear interrupt status */
        GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status & BIT(cf_gpio));

        /* Enable interrupt */
        gpio_pin_intr_state_set(GPIO_ID_PIN(cf_gpio), GPIO_PIN_INTR_NEGEDGE);
    }

    if ((cf1_gpio >= 0) && (gpio_status & BIT(cf1_gpio))) {
        if ((now - extra_info->first_cf1_interrupt_time) > PULSE_TIMEOUT) {
            uint32 pulse_width;

            if (extra_info->last_cf1_interrupt_time == extra_info->first_cf1_interrupt_time) {
                pulse_width = 0;
            }
            else {
                pulse_width = now - extra_info->last_cf1_interrupt_time;
            }

            /* Consider current mode and switch it */
            if (extra_info->mode == get_current_mode_sel(port)) {
                extra_info->current_pulse_width = pulse_width;
            }
            else { /* Assuming voltage mode */
                extra_info->voltage_pulse_width = pulse_width;
            }

            extra_info->mode = 1 - extra_info->mode;

            if (get_sel_gpio(port) >= 0) {
                gpio_write_value(get_sel_gpio(port), extra_info->mode);
            }

            extra_info->first_cf1_interrupt_time = now;
        }

        extra_info->last_cf1_interrupt_time = now;

        /* Disable interrupt */
        gpio_pin_intr_state_set(GPIO_ID_PIN(cf1_gpio), GPIO_PIN_INTR_DISABLE);

        /* Clear interrupt status */
        GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status & BIT(cf1_gpio));

        /* Enable interrupt */
        gpio_pin_intr_state_set(GPIO_ID_PIN(cf1_gpio), GPIO_PIN_INTR_NEGEDGE);
    }
}

void check_cf1_toggle_mode(port_t *port) {
    extra_info_t *extra_info = port->extra_info;
    uint64 now = system_uptime_us();

    if ((now - extra_info->last_cf1_interrupt_time) <= PULSE_TIMEOUT) {
        return;
    }

    if (extra_info->mode == get_current_mode_sel(port)) {
        extra_info->current_pulse_width = 0;
    }
    else {
        extra_info->voltage_pulse_width = 0;
    }

    extra_info->mode = 1 - extra_info->mode;
    extra_info->last_cf1_interrupt_time = extra_info->first_cf1_interrupt_time = now;

    if (get_sel_gpio(port) >= 0) {
        gpio_write_value(get_sel_gpio(port), extra_info->mode);
    }
}


void hlw8012_init_ports(void) {
#ifdef HAS_HLW8012_VOLTAGE
    port_register(hlw8012_voltage);
#endif
#ifdef HAS_HLW8012_CURRENT
    port_register(hlw8012_current);
#endif
#ifdef HAS_HLW8012_ACT_POW
    port_register(hlw8012_act_pow);
#endif
#ifdef HAS_HLW8012_ENERGY
    port_register(hlw8012_energy);
#endif
}


#endif  /* HAS_HLW8012 */
