
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
 * Inspired from https://github.com/arendst/Tasmota.
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
#include "extra/bl0940.h"


#ifdef HAS_BL0940

#define UART_NO                         UART0
#define UART_BAUD                       4800
#define UART_PARITY                     UART_PARITY_NONE
#define UART_STOP_BITS                  UART_STOP_BITS_15

#define VREF                            33000
#define IREF                            275000
#define PREF                            1430
#define ECOEF                           (1638.4 * 256 / PREF / 3600)
#define TTRANSFORM(t)                   (170 / 448.0 * ((t) / 2.0 - 32) - 45)

#define MIN_SAMP_INT                    1000    /* Milliseconds */
#define DEF_SAMP_INT                    5000    /* Milliseconds */
#define MAX_SAMP_INT                    3600000 /* Milliseconds */

#define WRITE_REQ_TIMEOUT               10000   /* Microseconds */
#define WAIT_RESP_TIMEOUT               50000   /* Microseconds */
#define READ_RESP_TIMEOUT               100000  /* Microseconds */
#define REQUEST                         {0x50, 0xAA}
#define RESPONSE_HEADER                 {0x55}
#define RESPONSE_LEN                    35


typedef struct {

    double                              last_energy;
    double                              last_voltage;
    double                              last_current;
    double                              last_active_power;
    double                              last_int_temp;
    uint64                              last_read_time; /* Milliseconds */
    bool                                configured;

} extra_info_t;


ICACHE_FLASH_ATTR static void           configure(port_t *port);

#ifdef HAS_BL0940_ENERGY
ICACHE_FLASH_ATTR static double         read_energy(port_t *port);
#endif

#ifdef HAS_BL0940_VOLTAGE
ICACHE_FLASH_ATTR static double         read_voltage(port_t *port);
#endif

#ifdef HAS_BL0940_CURRENT
ICACHE_FLASH_ATTR static double         read_current(port_t *port);
#endif

#ifdef HAS_BL0940_ACT_POW
ICACHE_FLASH_ATTR static double         read_act_pow(port_t *port);
#endif

#ifdef HAS_BL0940_INT_TEMP
ICACHE_FLASH_ATTR static double         read_int_temp(port_t *port);
#endif

ICACHE_FLASH_ATTR static bool           read_status_if_needed(port_t *port);
ICACHE_FLASH_ATTR static bool           read_status(port_t *port);


static extra_info_t bl0940_extra_info;


#ifdef HAS_BL0940_ENERGY

static port_t _bl0940_energy = {

    .slot = PORT_SLOT_AUTO,

    .id = BL0940_ENERGY_ID,
    .type = PORT_TYPE_NUMBER,
    .unit = "Wh",
    .min = UNDEFINED,
    .max = UNDEFINED,
    .step = UNDEFINED,

    .extra_info = &bl0940_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_energy,
    .configure = configure

};

port_t *bl0940_energy = &_bl0940_energy;

#endif


#ifdef HAS_BL0940_VOLTAGE

static port_t _bl0940_voltage = {

    .slot = PORT_SLOT_AUTO,

    .id = BL0940_VOLTAGE_ID,
    .type = PORT_TYPE_NUMBER,
    .unit = "V",
    .min = UNDEFINED,
    .max = UNDEFINED,
    .step = UNDEFINED,

    .extra_info = &bl0940_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_voltage,
    .configure = configure

};

port_t *bl0940_voltage = &_bl0940_voltage;

#endif


#ifdef HAS_BL0940_CURRENT

static port_t _bl0940_current = {

    .slot = PORT_SLOT_AUTO,

    .id = BL0940_CURRENT_ID,
    .type = PORT_TYPE_NUMBER,
    .unit = "A",
    .min = UNDEFINED,
    .max = UNDEFINED,
    .step = UNDEFINED,

    .extra_info = &bl0940_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_current,
    .configure = configure

};

port_t *bl0940_current = &_bl0940_current;

#endif


#ifdef HAS_BL0940_ACT_POW

static port_t _bl0940_act_pow = {

    .slot = PORT_SLOT_AUTO,

    .id = BL0940_ACT_POW_ID,
    .type = PORT_TYPE_NUMBER,
    .unit = "W",
    .min = UNDEFINED,
    .max = UNDEFINED,
    .step = UNDEFINED,

    .extra_info = &bl0940_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_act_pow,
    .configure = configure

};

port_t *bl0940_act_pow = &_bl0940_act_pow;

#endif


#ifdef HAS_BL0940_INT_TEMP

static port_t _bl0940_int_temp = {

    .slot = PORT_SLOT_AUTO,

    .id = BL0940_INT_TEMP_ID,
    .type = PORT_TYPE_NUMBER,
    .unit = "C",
    .min = UNDEFINED,
    .max = UNDEFINED,
    .step = UNDEFINED,

    .extra_info = &bl0940_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_int_temp,
    .configure = configure

};

port_t *bl0940_int_temp = &_bl0940_int_temp;

#endif


void configure(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    /* Prevent multiple serial port setups */
    if (!extra_info->configured) {
        DEBUG_BL0940(port, "configuring serial port");
        uart_setup(UART_NO, UART_BAUD, UART_PARITY, UART_STOP_BITS);
        extra_info->configured = TRUE;

        /* Set initial values to UNDEFINED */
        extra_info->last_energy = UNDEFINED;
        extra_info->last_voltage = UNDEFINED;
        extra_info->last_current = UNDEFINED;
        extra_info->last_int_temp = UNDEFINED;
    }
}

#ifdef HAS_BL0940_ENERGY
double read_energy(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    if (!read_status_if_needed(port)) {
        return UNDEFINED;
    }

    return extra_info->last_energy;
}
#endif

#ifdef HAS_BL0940_VOLTAGE
double read_voltage(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    if (!read_status_if_needed(port)) {
        return UNDEFINED;
    }

    return extra_info->last_voltage;
}
#endif

#ifdef HAS_BL0940_CURRENT
double read_current(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    if (!read_status_if_needed(port)) {
        return UNDEFINED;
    }

    return extra_info->last_current;
}
#endif

#ifdef HAS_BL0940_ACT_POW
double read_act_pow(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    if (!read_status_if_needed(port)) {
        return UNDEFINED;
    }

    return extra_info->last_active_power;
}
#endif

#ifdef HAS_BL0940_INT_TEMP
double read_int_temp(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    if (!read_status_if_needed(port)) {
        return UNDEFINED;
    }

    return extra_info->last_int_temp;
}
#endif

bool read_status_if_needed(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    uint64 now_ms = system_uptime_ms();
    uint64 delta = now_ms - extra_info->last_read_time;
    if (delta >= port->sampling_interval - 10) { /* Allow 10 milliseconds of tolerance */
        DEBUG_BL0940(port, "status needs new reading");

        /* Update last read time */
        extra_info->last_read_time = now_ms;

        if (read_status(port)) {
            return TRUE;
        }

        DEBUG_BL0940(port, "status reading failed");

        /* In case of error, cached status can be used within up to twice the sampling interval. */
        if (delta > port->sampling_interval * 2) {
            return FALSE;
        }
    }

    return TRUE;
}

bool read_status(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    /* Write request */
    static uint8 request[] = REQUEST;
    uint16 size = uart_write(UART_NO, request, sizeof(request), WRITE_REQ_TIMEOUT);
    if (size!= sizeof(request)) {
        DEBUG_BL0940(port, "failed to write request: %d/%d bytes written", size, sizeof(request));
        return FALSE;
    }
    DEBUG_BL0940(port, "request sent");

    /* Read response */
    static uint8 read_buff[RESPONSE_LEN];
    uint16 i;

    size = uart_read(UART_NO, read_buff, RESPONSE_LEN, WAIT_RESP_TIMEOUT + READ_RESP_TIMEOUT);
    if (size != RESPONSE_LEN) {
        DEBUG_BL0940(port, "failed to read response: %d/%d bytes read", size, RESPONSE_LEN);
        return FALSE;
    }

    /* Validate header */
    static uint8 response_header[] = RESPONSE_HEADER;
    if (memcmp(read_buff, response_header, sizeof(response_header))) {
        DEBUG_BL0940(port, "failed to read response: unexpected response header");
        return FALSE;
    }

    /* Compute & validate checksum */
    uint8 checksum = 0x50;
    for (i = 0; i < size - 1; i++) {
        checksum += read_buff[i];
    }

    checksum = ~checksum;

    if (checksum != read_buff[size - 1]) {
        DEBUG_BL0940(port, "failed to read response: invalid checksum (expected %02X, got %02X)",
                     checksum, read_buff[size - 1]);
        return FALSE;
    }

    static uint8 *p_value;
    static uint32 int_value;
    static int32 sint_value;

    /* Parse voltage */
    p_value = read_buff + 10;
    int_value = (p_value[2] << 16) + (p_value[1] << 8) + p_value[0];
    extra_info->last_voltage = round_to((float) int_value / VREF, 1);
    DEBUG_BL0940(port, "read voltage: %d/%d = %s V", int_value, VREF, dtostr(extra_info->last_voltage, -1));

    /* Parse current */
    p_value = read_buff + 4;
    int_value = (p_value[2] << 16) + (p_value[1] << 8) + p_value[0];
    extra_info->last_current = round_to((float) int_value / IREF, 3);
    DEBUG_BL0940(port, "read current: %d/%d = %s A", int_value, IREF, dtostr(extra_info->last_current, -1));

    /* Parse power */
    p_value = read_buff + 16;
    sint_value = ((p_value[2] << 16) + (p_value[1] << 8) + p_value[0]) << 8;
    sint_value = abs(sint_value); /* It seems that power is transmitted with sign */
    int_value = sint_value >> 8;
    extra_info->last_active_power = round_to((float) int_value / PREF, 2);
    DEBUG_BL0940(port, "read power: %d/%d = %s W", int_value, PREF, dtostr(extra_info->last_active_power, -1));

    /* Parse energy */
    p_value = read_buff + 22;
    int_value = (p_value[2] << 16) + (p_value[1] << 8) + p_value[0];
    extra_info->last_energy = round(int_value * ECOEF);
    DEBUG_BL0940(port, "read energy: %d * %s = %s Wh",
                 int_value, dtostr(ECOEF, -1),
                 dtostr(extra_info->last_energy, -1));

    /* Parse internal temperature */
    p_value = read_buff + 28;
    int_value = (p_value[1] << 8) + p_value[0];
    extra_info->last_int_temp = round_to(TTRANSFORM(int_value), 1);
    DEBUG_BL0940(port, "read int temperature: f(%d) = %s C", int_value, dtostr(extra_info->last_int_temp, -1));

    return TRUE;
}


void bl0940_init_ports(void) {
#ifdef HAS_BL0940_ENERGY
    port_register(bl0940_energy);
#endif
#ifdef HAS_BL0940_VOLTAGE
    port_register(bl0940_voltage);
#endif
#ifdef HAS_BL0940_CURRENT
    port_register(bl0940_current);
#endif
#ifdef HAS_BL0940_ACT_POW
    port_register(bl0940_act_pow);
#endif
#ifdef HAS_BL0940_INT_TEMP
    port_register(bl0940_int_temp);
#endif
}


#endif  /* HAS_BL0940 */
