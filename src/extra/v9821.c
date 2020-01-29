
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
 */

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <mem.h>
#include <user_interface.h>
#include <gpio.h>

#include "espgoodies/common.h"
#include "espgoodies/system.h"
#include "espgoodies/utils.h"

#include "api.h"
#include "apiutils.h"
#include "common.h"
#include "drivers/uart.h"
#include "ports.h"
#include "extra/v9821.h"


#ifdef HAS_V9821

#define UART_NO                         UART0
#define UART_BAUD                       9600
#define UART_PARITY                     UART_PARITY_EVEN
#define UART_STOP_BITS                  UART_STOP_BITS_1

#define MIN_SAMP_INT                    1000    /* milliseconds */
#define DEF_SAMP_INT                    5000    /* milliseconds */
#define MAX_SAMP_INT                    3600000 /* milliseconds */

#define WRITE_REQ_TIMEOUT               20000   /* microseconds */
#define WAIT_RESP_TIMEOUT               70000   /* microseconds */
#define READ_RESP_TIMEOUT               50000   /* microseconds */
#define REQUEST                         {0xFE, 0x01, 0x0F, 0x08, 0x00, 0x00, 0x00, 0x1C}
#define RESPONSE_HEADER                 {0xFE, 0x01, 0x08}
#define RESPONSE_LEN                    36


typedef struct {

    double                              last_energy;
    double                              last_voltage;
    double                              last_current;
    double                              last_freq;
    double                              last_active_power;
    double                              last_reactive_power;
    double                              last_apparent_power;
    double                              last_power_factor;
    uint64                              last_read_time; /* milliseconds */

} extra_info_t;


ICACHE_FLASH_ATTR static void           configure(port_t *port);

#ifdef HAS_V9821_ENERGY
ICACHE_FLASH_ATTR static double         read_energy(port_t *port);
#endif

#ifdef HAS_V9821_VOLTAGE
ICACHE_FLASH_ATTR static double         read_voltage(port_t *port);
#endif

#ifdef HAS_V9821_CURRENT
ICACHE_FLASH_ATTR static double         read_current(port_t *port);
#endif

#ifdef HAS_V9821_FREQ
ICACHE_FLASH_ATTR static double         read_freq(port_t *port);
#endif

#ifdef HAS_V9821_ACT_POW
ICACHE_FLASH_ATTR static double         read_act_pow(port_t *port);
#endif

#ifdef HAS_V9821_REA_POW
ICACHE_FLASH_ATTR static double         read_rea_pow(port_t *port);
#endif

#ifdef HAS_V9821_APP_POW
ICACHE_FLASH_ATTR static double         read_app_pow(port_t *port);
#endif

#ifdef HAS_V9821_POW_FACT
ICACHE_FLASH_ATTR static double         read_pow_fact(port_t *port);
#endif

ICACHE_FLASH_ATTR static bool           read_status_if_needed(port_t *port);
ICACHE_FLASH_ATTR static bool           read_status(port_t *port);


static extra_info_t v9821_extra_info;


#ifdef HAS_V9821_ENERGY

static port_t _v9821_energy = {

    .slot = PORT_SLOT_AUTO,

    .id = V9821_ENERGY_ID,
    .type = PORT_TYPE_NUMBER,
    .step = UNDEFINED,

    .extra_info = &v9821_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_energy,
    .configure = configure

};

port_t *v9821_energy = &_v9821_energy;

#endif


#ifdef HAS_V9821_VOLTAGE

static port_t _v9821_voltage = {

    .slot = PORT_SLOT_AUTO,

    .id = V9821_VOLTAGE_ID,
    .type = PORT_TYPE_NUMBER,
    .step = UNDEFINED,

    .extra_info = &v9821_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_voltage,
    .configure = configure

};

port_t *v9821_voltage = &_v9821_voltage;

#endif


#ifdef HAS_V9821_CURRENT

static port_t _v9821_current = {

    .slot = PORT_SLOT_AUTO,

    .id = V9821_CURRENT_ID,
    .type = PORT_TYPE_NUMBER,
    .step = UNDEFINED,

    .extra_info = &v9821_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_current,
    .configure = configure

};

port_t *v9821_current = &_v9821_current;

#endif


#ifdef HAS_V9821_FREQ

static port_t _v9821_freq = {

    .slot = PORT_SLOT_AUTO,

    .id = V9821_FREQ_ID,
    .type = PORT_TYPE_NUMBER,
    .step = UNDEFINED,

    .extra_info = &v9821_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_freq,
    .configure = configure

};

port_t *v9821_freq = &_v9821_freq;

#endif


#ifdef HAS_V9821_ACT_POW

static port_t _v9821_act_pow = {

    .slot = PORT_SLOT_AUTO,

    .id = V9821_ACT_POW_ID,
    .type = PORT_TYPE_NUMBER,
    .step = UNDEFINED,

    .extra_info = &v9821_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_act_pow,
    .configure = configure

};

port_t *v9821_act_pow = &_v9821_act_pow;

#endif


#ifdef HAS_V9821_REA_POW

static port_t _v9821_rea_pow = {

    .slot = PORT_SLOT_AUTO,

    .id = V9821_REA_POW_ID,
    .type = PORT_TYPE_NUMBER,
    .step = UNDEFINED,

    .extra_info = &v9821_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_rea_pow,
    .configure = configure

};

port_t *v9821_rea_pow = &_v9821_rea_pow;

#endif


#ifdef HAS_V9821_APP_POW

static port_t _v9821_app_pow = {

    .slot = PORT_SLOT_AUTO,

    .id = V9821_APP_POW_ID,
    .type = PORT_TYPE_NUMBER,
    .step = UNDEFINED,

    .extra_info = &v9821_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_app_pow,
    .configure = configure

};

port_t *v9821_app_pow = &_v9821_app_pow;

#endif


#ifdef HAS_V9821_POW_FACT

static port_t _v9821_pow_fact = {

    .slot = PORT_SLOT_AUTO,

    .id = V9821_POW_FACT_ID,
    .type = PORT_TYPE_NUMBER,
    .step = UNDEFINED,

    .extra_info = &v9821_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_pow_fact,
    .configure = configure

};

port_t *v9821_pow_fact = &_v9821_pow_fact;

#endif


void configure(port_t *port) {
    DEBUG_V9821(port, "configuring serial port");
    uart_setup(UART_NO, UART_BAUD, UART_PARITY, UART_STOP_BITS);
}

#ifdef HAS_V9821_ENERGY
double read_energy(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    if (!read_status_if_needed(port)) {
        return UNDEFINED;
    }

    return extra_info->last_energy;
}
#endif

#ifdef HAS_V9821_VOLTAGE
double read_voltage(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    if (!read_status_if_needed(port)) {
        return UNDEFINED;
    }

    return extra_info->last_voltage;
}
#endif

#ifdef HAS_V9821_CURRENT
double read_current(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    if (!read_status_if_needed(port)) {
        return UNDEFINED;
    }

    return extra_info->last_current;
}
#endif

#ifdef HAS_V9821_FREQ
double read_freq(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    if (!read_status_if_needed(port)) {
        return UNDEFINED;
    }

    return extra_info->last_freq;
}
#endif

#ifdef HAS_V9821_ACT_POW
double read_act_pow(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    if (!read_status_if_needed(port)) {
        return UNDEFINED;
    }

    return extra_info->last_active_power;
}
#endif

#ifdef HAS_V9821_REA_POW
double read_rea_pow(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    if (!read_status_if_needed(port)) {
        return UNDEFINED;
    }

    return extra_info->last_reactive_power;
}
#endif

#ifdef HAS_V9821_APP_POW
double read_app_pow(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    if (!read_status_if_needed(port)) {
        return UNDEFINED;
    }

    return extra_info->last_apparent_power;
}
#endif

#ifdef HAS_V9821_POW_FACT
double read_pow_fact(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    if (!read_status_if_needed(port)) {
        return UNDEFINED;
    }

    return extra_info->last_power_factor;
}
#endif

bool read_status_if_needed(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    uint64 now = system_uptime_us() / 1000;
    uint64 delta = now - extra_info->last_read_time;
    if (delta > port->sampling_interval) {
        DEBUG_V9821(port, "status needs new reading");

        /* update last read time */
        extra_info->last_read_time = system_uptime_us() / 1000;

        if (!read_status(port)) {
            DEBUG_V9821(port, "status reading failed");
            return FALSE;
        }
    }

    return TRUE;
}

bool read_status(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    /* write request */
    static uint8 request[] = REQUEST;
    uint16 size = uart_write(UART_NO, request, sizeof(request), WRITE_REQ_TIMEOUT);
    if (size!= sizeof(request)) {
        DEBUG_V9821(port, "failed to write request: %d/%d bytes written", size, sizeof(request));
        return FALSE;
    }
    DEBUG_V9821(port, "request sent");

    /* read response */
    static uint8 read_buff[RESPONSE_LEN];
    uint16 i;

    size = uart_read(UART_NO, read_buff, RESPONSE_LEN, WAIT_RESP_TIMEOUT + READ_RESP_TIMEOUT);
    if (size != RESPONSE_LEN) {
        DEBUG_V9821(port, "failed to read response: %d/%d bytes read", size, RESPONSE_LEN);
        return FALSE;
    }

    /* validate header */
    static uint8 response_header[] = RESPONSE_HEADER;
    if (memcmp(read_buff, response_header, sizeof(response_header))) {
        DEBUG_V9821(port, "failed to read response: unexpected response header");
        return FALSE;
    }

    /* compute & validate checksum */
    uint8 checksum = 0;
    for (i = 0; i < size - 1; i++) {
        checksum += read_buff[i];
    }

    checksum = ~checksum;
    checksum += 0x33;

    if (checksum != read_buff[size - 1]) {
        DEBUG_V9821(port, "failed to read response: invalid checksum (expected %02X, got %02X)",
                    checksum, read_buff[size - 1]);
        return FALSE;
    }

    static uint8 *p_value;
    static char hex_value[9];
    static uint32 int_value;

    /* parse energy */
    p_value = read_buff + 3;
    snprintf(hex_value, sizeof(hex_value), "%02X%02X%02X%02X", p_value[3], p_value[2], p_value[1], p_value[0]);
    int_value = strtol(hex_value, NULL, 10);
    DEBUG_V9821(port, "read energy: %d/100 kWh", int_value);
    extra_info->last_energy = int_value / 100.0;

    /* parse voltage */
    p_value = read_buff + 7;
    snprintf(hex_value, sizeof(hex_value), "%02X%02X%02X%02X", p_value[3], p_value[2], p_value[1], p_value[0]);
    int_value = strtol(hex_value, NULL, 10);
    DEBUG_V9821(port, "read voltage: %d/10 V", int_value);
    extra_info->last_voltage = int_value / 10.0;

    /* parse current */
    p_value = read_buff + 11;
    snprintf(hex_value, sizeof(hex_value), "%02X%02X%02X%02X", p_value[3], p_value[2], p_value[1], p_value[0]);
    int_value = strtol(hex_value, NULL, 10);
    DEBUG_V9821(port, "read current: %d/10000 A", int_value);
    extra_info->last_current = int_value / 10000.0;

    /* parse frequency */
    p_value = read_buff + 15;
    snprintf(hex_value, sizeof(hex_value), "%02X%02X%02X%02X", p_value[3], p_value[2], p_value[1], p_value[0]);
    int_value = strtol(hex_value, NULL, 10);
    DEBUG_V9821(port, "read frequency: %d/100 Hz", int_value);
    extra_info->last_freq = int_value / 100.0;

    /* parse active power */
    p_value = read_buff + 19;
    snprintf(hex_value, sizeof(hex_value), "%02X%02X%02X%02X", p_value[3], p_value[2], p_value[1], p_value[0]);
    int_value = strtol(hex_value, NULL, 10);
    DEBUG_V9821(port, "read active power: %d/100 W", int_value);
    extra_info->last_active_power = int_value / 100.0;

    /* parse reactive power */
    p_value = read_buff + 23;
    snprintf(hex_value, sizeof(hex_value), "%02X%02X%02X%02X", p_value[3], p_value[2], p_value[1], p_value[0]);
    int_value = strtol(hex_value, NULL, 10);
    DEBUG_V9821(port, "read reactive power: %d/100 W", int_value);
    extra_info->last_reactive_power = int_value / 100.0;

    /* parse apparent power */
    p_value = read_buff + 27;
    snprintf(hex_value, sizeof(hex_value), "%02X%02X%02X%02X", p_value[3], p_value[2], p_value[1], p_value[0]);
    int_value = strtol(hex_value, NULL, 10);
    DEBUG_V9821(port, "read apparent power: %d/100 W", int_value);
    extra_info->last_apparent_power = int_value / 100.0;

    /* parse power factor */
    p_value = read_buff + 31;
    snprintf(hex_value, sizeof(hex_value), "%02X%02X%02X%02X", p_value[3], p_value[2], p_value[1], p_value[0]);
    int_value = strtol(hex_value, NULL, 10);
    DEBUG_V9821(port, "read power factor: %d/10 %%", int_value);
    extra_info->last_power_factor = int_value / 10.0;

    return TRUE;
}


void v9821_init_ports(void) {
#ifdef HAS_V9821_ENERGY
    port_register(v9821_energy);
#endif
#ifdef HAS_V9821_VOLTAGE
    port_register(v9821_voltage);
#endif
#ifdef HAS_V9821_CURRENT
    port_register(v9821_current);
#endif
#ifdef HAS_V9821_FREQ
    port_register(v9821_freq);
#endif
#ifdef HAS_V9821_ACT_POW
    port_register(v9821_act_pow);
#endif
#ifdef HAS_V9821_REA_POW
    port_register(v9821_rea_pow);
#endif
#ifdef HAS_V9821_APP_POW
    port_register(v9821_app_pow);
#endif
#ifdef HAS_V9821_POW_FACT
    port_register(v9821_pow_fact);
#endif
}


#endif  /* HAS_V9821 */
