
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

#define CF_GPIO_CONFIG_OFFS             0x00 /* 1 byte */
#define CF1_GPIO_CONFIG_OFFS            0x01 /* 1 byte */
#define SEL_GPIO_CONFIG_OFFS            0x02 /* 1 byte */

#define FLAG_CURRENT_MODE_SEL           PORT_FLAG_CUSTOM0
#define FLAG_BIT_CURRENT_MODE_SEL       PORT_FLAG_BIT_CUSTOM0


typedef struct {

    int8                                cf_gpio;
    int8                                cf1_gpio;
    int8                                sel_gpio;

    uint64                              last_cf1_switch_time;       /* Microseconds */
    uint64                              last_cf1_interrupt_time;    /* Microseconds */
    uint64                              last_cf_interrupt_time;     /* Microseconds */

    uint64                              power_pulse_width_sum;
    uint32                              power_pulse_count;
    uint64                              total_power_pulse_count;
    uint64                              current_pulse_width_sum;
    uint32                              current_pulse_count;
    uint64                              voltage_pulse_width_sum;
    uint32                              voltage_pulse_count;

    uint8                               mode;

} extra_info_t;


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

static void                             gpio_interrupt_handler(void *arg);


static attrdef_t cf_gpio_attrdef = {

    .name = "cf_gpio",
    .display_name = "CF GPIO Number",
    .description = "The GPIO where the CF pin is attached.",
    .type = ATTR_TYPE_NUMBER,
    .choices = all_gpio_none_choices,
    .modifiable = TRUE,
    .def = -1,
    .gs_type = ATTRDEF_GS_TYPE_EXTRA_DATA_1BS,
    .gs_extra_data_offs = CF_GPIO_CONFIG_OFFS,
    .extra_info_cache_offs = ATTRDEF_CACHE_EXTRA_INFO_FIELD(extra_info_t, cf_gpio)

};

static attrdef_t cf1_gpio_attrdef = {

    .name = "cf1_gpio",
    .display_name = "CF1 GPIO Number",
    .description = "The GPIO where the CF1 pin is attached.",
    .type = ATTR_TYPE_NUMBER,
    .choices = all_gpio_none_choices,
    .modifiable = TRUE,
    .def = -1,
    .gs_type = ATTRDEF_GS_TYPE_EXTRA_DATA_1BS,
    .gs_extra_data_offs = CF1_GPIO_CONFIG_OFFS,
    .extra_info_cache_offs = ATTRDEF_CACHE_EXTRA_INFO_FIELD(extra_info_t, cf1_gpio)

};

static attrdef_t sel_gpio_attrdef = {

    .name = "sel_gpio",
    .display_name = "SEL GPIO Number",
    .description = "The GPIO where the SEL pin is attached.",
    .type = ATTR_TYPE_NUMBER,
    .choices = all_gpio_none_choices,
    .modifiable = TRUE,
    .def = -1,
    .gs_type = ATTRDEF_GS_TYPE_EXTRA_DATA_1BS,
    .gs_extra_data_offs = SEL_GPIO_CONFIG_OFFS,
    .extra_info_cache_offs = ATTRDEF_CACHE_EXTRA_INFO_FIELD(extra_info_t, sel_gpio)

};

static attrdef_t current_mode_sel_attrdef = {

    .name = "current_mode_sel",
    .display_name = "Current Mode SEL Level",
    .description = "Indicates the SEL pin level for current mode.",
    .type = ATTR_TYPE_BOOLEAN,
    .modifiable = TRUE,
    .integer = TRUE,
    .gs_type = ATTRDEF_GS_TYPE_FLAG,
    .gs_flag_bit = FLAG_BIT_CURRENT_MODE_SEL

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

    int8 cf_gpio = extra_info->cf_gpio;
    if (cf_gpio >= 0) {
        gpio_configure_input(cf_gpio, /* pull = */ TRUE);
        gpio_register_set(GPIO_PIN_ADDR(cf_gpio),
                          GPIO_PIN_INT_TYPE_SET(GPIO_PIN_INTR_DISABLE) |
                          GPIO_PIN_PAD_DRIVER_SET(GPIO_PAD_DRIVER_DISABLE) |
                          GPIO_PIN_SOURCE_SET(GPIO_AS_PIN_SOURCE));
        GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, BIT(cf_gpio));
        gpio_pin_intr_state_set(cf_gpio, GPIO_PIN_INTR_NEGEDGE);
    }

    int8 cf1_gpio = extra_info->cf1_gpio;
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

    int8 sel_gpio = extra_info->sel_gpio;
    if (sel_gpio >= 0) {
        gpio_configure_output(sel_gpio, /* initial = */ extra_info->mode);
    }

    ETS_GPIO_INTR_ENABLE();
}

#ifdef HAS_HLW8012_VOLTAGE
double read_voltage(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    double voltage = 0;
    if (extra_info->voltage_pulse_count && extra_info->voltage_pulse_width_sum) {
        voltage = 1e6F * extra_info->voltage_pulse_count / extra_info->voltage_pulse_width_sum;
    }

    DEBUG_HLW8012("read voltage = %s", dtostr(voltage, -1));

    extra_info->voltage_pulse_width_sum = 0;
    extra_info->voltage_pulse_count = 0;

    return voltage;
}
#endif

#ifdef HAS_HLW8012_CURRENT
double read_current(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    double current = 0;
    if (extra_info->current_pulse_count && extra_info->current_pulse_width_sum) {
        current = 1e6F * extra_info->current_pulse_count / extra_info->current_pulse_width_sum;
    }

    DEBUG_HLW8012("read current = %s", dtostr(current, -1));

    extra_info->current_pulse_width_sum = 0;
    extra_info->current_pulse_count = 0;

    return current;
}
#endif

#ifdef HAS_HLW8012_ACT_POW
double read_act_pow(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    double power = 0;
    if (extra_info->power_pulse_count && extra_info->power_pulse_width_sum) {
        power = 1e6F * extra_info->power_pulse_count / extra_info->power_pulse_width_sum;
    }

    DEBUG_HLW8012("read power = %s", dtostr(power, -1));

    extra_info->power_pulse_width_sum = 0;
    extra_info->power_pulse_count = 0;

    return power;
}
#endif

#ifdef HAS_HLW8012_ENERGY
double read_energy(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    double energy = extra_info->total_power_pulse_count / 1000.0;
    DEBUG_HLW8012("read energy = %s", dtostr(energy, -1));

    return energy;
}
#endif


void gpio_interrupt_handler(void *arg) {
    port_t *port = arg;
    extra_info_t *extra_info = port->extra_info;

    uint64 now_us = system_uptime_us();
    int8 cf_gpio = extra_info->cf_gpio;
    int8 cf1_gpio = extra_info->cf1_gpio;
    int8 sel_gpio = extra_info->sel_gpio;
    uint32 gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);

    if ((cf_gpio >= 0) && (gpio_status & BIT(cf_gpio))) {
        extra_info->power_pulse_width_sum += now_us - extra_info->last_cf_interrupt_time;
        extra_info->power_pulse_count++;
        extra_info->total_power_pulse_count++;
        extra_info->last_cf_interrupt_time = now_us;
    }

    if ((cf1_gpio >= 0) && (gpio_status & BIT(cf1_gpio))) {
        uint32 pulse_width = now_us - extra_info->last_cf1_interrupt_time;

        if (extra_info->mode == get_current_mode_sel(port)) {
            extra_info->current_pulse_width_sum += pulse_width;
            extra_info->current_pulse_count++;
        }
        else { /* Assuming voltage mode */
            extra_info->voltage_pulse_width_sum += pulse_width;
            extra_info->voltage_pulse_count++;
        }

        extra_info->last_cf1_interrupt_time = now_us;

        /* Switch from voltage to current mode, if enough time has passed in currently selected mode */
        if (cf1_gpio >= 0) {
            if ((now_us - extra_info->last_cf1_switch_time) > 5e5) {
                extra_info->last_cf1_switch_time = now_us;
                extra_info->mode = 1 - extra_info->mode;

                if (sel_gpio >= 0) {
                    gpio_write_value(sel_gpio, extra_info->mode);
                }
            }
        }
    }

    GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status);
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
