
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

#include <stdlib.h>
#include <c_types.h>
#include <ets_sys.h>
#include <user_interface.h>
#include <limits.h>

#include "espgoodies/common.h"
#include "espgoodies/debug.h"
#include "espgoodies/drivers/uart.h"
#include "espgoodies/system.h"
#include "espgoodies/utils.h"

#include "common.h"
#include "peripherals.h"
#include "ports.h"

#include "peripherals/bl0940.h"


#define MIN_SAMP_INT 1000    /* Milliseconds */
#define DEF_SAMP_INT 5000    /* Milliseconds */
#define MAX_SAMP_INT 3600000 /* Milliseconds */

#define UART_NO        UART0
#define UART_BAUD      4800
#define UART_PARITY    UART_PARITY_NONE
#define UART_STOP_BITS UART_STOP_BITS_15

#define VREF          33000
#define IREF          275000
#define PREF          1430
#define ECOEF         (1638.4 * 256 / PREF / 3600)
#define TTRANSFORM(t) ((170 / 448.0 * ((t) / 2.0 - 32)) - 45)

#define WRITE_REQ_TIMEOUT 10000  /* Microseconds */
#define WAIT_RESP_TIMEOUT 50000  /* Microseconds */
#define READ_RESP_TIMEOUT 100000 /* Microseconds */
#define REQUEST           {0x50, 0xAA}
#define RESPONSE_HEADER   {0x55}
#define RESPONSE_LEN      35


typedef struct {

    double  last_active_power;
    double  last_energy;
    double  last_current;
    double  last_voltage;
    double  last_temperature;
    int64   last_read_time_ms;
    bool    configured;

    port_t *active_power_port; /* Needed for sampling interval */

} user_data_t;


static double ICACHE_FLASH_ATTR read_active_power(port_t *port);
static double ICACHE_FLASH_ATTR read_energy(port_t *port);
static double ICACHE_FLASH_ATTR read_current(port_t *port);
static double ICACHE_FLASH_ATTR read_voltage(port_t *port);
static double ICACHE_FLASH_ATTR read_temperature(port_t *port);

static bool   ICACHE_FLASH_ATTR read_data(peripheral_t *peripheral);
static void   ICACHE_FLASH_ATTR read_data_if_needed(peripheral_t *peripheral);

static void   ICACHE_FLASH_ATTR reset_read_values(user_data_t *user_data);

static void   ICACHE_FLASH_ATTR configure(port_t *port, bool enabled);

static void   ICACHE_FLASH_ATTR init(peripheral_t *peripheral);
static void   ICACHE_FLASH_ATTR make_ports(peripheral_t *peripheral, port_t **ports, uint8 *ports_len);


peripheral_type_t peripheral_type_bl0940 = {

    .init = init,
    .make_ports = make_ports

};


double read_active_power(port_t *port) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;

    read_data_if_needed(peripheral);

    return user_data->last_active_power;
}

double read_energy(port_t *port) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;

    read_data_if_needed(peripheral);

    return user_data->last_energy;
}

double read_current(port_t *port) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;

    read_data_if_needed(peripheral);

    return user_data->last_current;
}

double read_voltage(port_t *port) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;

    read_data_if_needed(peripheral);

    return user_data->last_voltage;
}

double read_temperature(port_t *port) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = port->peripheral->user_data;

    read_data_if_needed(peripheral);

    return user_data->last_temperature;
}

bool read_data(peripheral_t *peripheral) {
    user_data_t *user_data = peripheral->user_data;

    /* Write request */
    static uint8 request[] = REQUEST;
    uint16 size = uart_write(UART_NO, request, sizeof(request), WRITE_REQ_TIMEOUT);
    if (size!= sizeof(request)) {
        DEBUG_BL0940(peripheral, "failed to write request: %d/%d bytes written", size, sizeof(request));
        return FALSE;
    }
    DEBUG_BL0940(peripheral, "request sent");

    /* Read response */
    static uint8 read_buff[RESPONSE_LEN];
    uint16 i;

    size = uart_read(UART_NO, read_buff, RESPONSE_LEN, WAIT_RESP_TIMEOUT + READ_RESP_TIMEOUT);
    if (size != RESPONSE_LEN) {
        DEBUG_BL0940(peripheral, "failed to read response: %d/%d bytes read", size, RESPONSE_LEN);
        return FALSE;
    }

    /* Validate header */
    static uint8 response_header[] = RESPONSE_HEADER;
    if (memcmp(read_buff, response_header, sizeof(response_header))) {
        DEBUG_BL0940(peripheral, "failed to read response: unexpected response header");
        return FALSE;
    }

    /* Compute & validate checksum */
    uint8 checksum = 0x50;
    for (i = 0; i < size - 1; i++) {
        checksum += read_buff[i];
    }

    checksum = ~checksum;

    if (checksum != read_buff[size - 1]) {
        DEBUG_BL0940(
            peripheral,
            "failed to read response: invalid checksum (expected %02X, got %02X)",
            checksum,
            read_buff[size - 1]
        );
        return FALSE;
    }

    static uint8 *p_value;
    static uint32 int_value;
    static int32 sint_value;

    /* Parse current */
    p_value = read_buff + 4;
    int_value = (p_value[2] << 16) + (p_value[1] << 8) + p_value[0];
    user_data->last_current = round_to((float) int_value / IREF, 3);
    DEBUG_BL0940(peripheral, "read current: %d/%d = %s A", int_value, IREF, dtostr(user_data->last_current, -1));

    /* Parse voltage */
    p_value = read_buff + 10;
    int_value = (p_value[2] << 16) + (p_value[1] << 8) + p_value[0];
    user_data->last_voltage = round_to((float) int_value / VREF, 1);
    DEBUG_BL0940(peripheral, "read voltage: %d/%d = %s V", int_value, VREF, dtostr(user_data->last_voltage, -1));

    /* Parse power */
    p_value = read_buff + 16;
    sint_value = ((p_value[2] << 16) + (p_value[1] << 8) + p_value[0]) << 8;
    sint_value = abs(sint_value); /* It seems that power is transmitted with sign */
    int_value = sint_value >> 8;
    user_data->last_active_power = round_to((float) int_value / PREF, 2);
    DEBUG_BL0940(
        peripheral,
        "read active power: %d/%d = %s W",
        int_value,
        PREF,
        dtostr(user_data->last_active_power, -1)
    );

    /* Parse energy */
    p_value = read_buff + 22;
    int_value = (p_value[2] << 16) + (p_value[1] << 8) + p_value[0];
    user_data->last_energy = round(int_value * ECOEF);
    DEBUG_BL0940(
        peripheral,
        "read energy: %d * %s = %s Wh",
        int_value,
        dtostr(ECOEF, -1),
        dtostr(user_data->last_energy, -1)
    );

    /* Parse temperature */
    p_value = read_buff + 28;
    int_value = (p_value[1] << 8) + p_value[0];
    user_data->last_temperature = round_to(TTRANSFORM(int_value), 1);
    DEBUG_BL0940(peripheral, "read temperature: f(%d) = %s C", int_value, dtostr(user_data->last_temperature, -1));

    return TRUE;
}

void read_data_if_needed(peripheral_t *peripheral) {
    user_data_t *user_data = peripheral->user_data;
    port_t *active_power_port = user_data->active_power_port;

    /* Use activer_power port's sampling interval for all data */

    uint64 now_ms = system_uptime_ms();
    uint64 delta = now_ms - user_data->last_read_time_ms;
    if (delta >= active_power_port->sampling_interval) {
        user_data->last_read_time_ms = now_ms;
        DEBUG_BL0940(peripheral, "data reading is needed");
        if (!read_data(peripheral)) {
            reset_read_values(user_data);
        }
    }
}

void reset_read_values(user_data_t *user_data) {
    user_data->last_active_power = UNDEFINED;
    user_data->last_energy = UNDEFINED;
    user_data->last_current = UNDEFINED;
    user_data->last_voltage = UNDEFINED;
    user_data->last_temperature = UNDEFINED;
}

void configure(port_t *port, bool enabled) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;

    reset_read_values(user_data);
    user_data->last_read_time_ms = -LLONG_MAX;

    /* Configure serial port once, upon enabling first port */
    if (enabled && !user_data->configured) {
#ifdef _DEBUG
        /* Switch debug to second UART */
        if (debug_uart_get_no() == UART_NO) {
            debug_uart_setup(1 - UART_NO, _DEBUG_UART_ALT);
        }
#endif
        DEBUG_BL0940(peripheral, "configuring serial port");
        uart_setup(UART_NO, UART_BAUD, UART_PARITY, UART_STOP_BITS, /* alt = */ FALSE);
        user_data->configured = TRUE;
    }
}

void init(peripheral_t *peripheral) {
    peripheral->user_data = zalloc(sizeof(user_data_t));
}

void make_ports(peripheral_t *peripheral, port_t **ports, uint8 *ports_len) {
    user_data_t *user_data = peripheral->user_data;

    port_t *active_power_port = port_new();
    active_power_port->slot = -1;
    active_power_port->type = PORT_TYPE_NUMBER;
    active_power_port->unit = "W";
    active_power_port->min_sampling_interval = MIN_SAMP_INT;
    active_power_port->max_sampling_interval = MAX_SAMP_INT;
    active_power_port->def_sampling_interval = DEF_SAMP_INT;
    active_power_port->read_value = read_active_power;
    active_power_port->configure = configure;
    ports[(*ports_len)++] = active_power_port;

    user_data->active_power_port = active_power_port;

    port_t *energy_port = port_new();
    energy_port->slot = -1;
    energy_port->type = PORT_TYPE_NUMBER;
    energy_port->unit = "Wh";
    energy_port->read_value = read_energy;
    energy_port->configure = configure;
    ports[(*ports_len)++] = energy_port;

    port_t *current_port = port_new();
    current_port->slot = -1;
    current_port->type = PORT_TYPE_NUMBER;
    current_port->unit = "A";
    current_port->read_value = read_current;
    current_port->configure = configure;
    ports[(*ports_len)++] = current_port;

    port_t *voltage_port = port_new();
    voltage_port->slot = -1;
    voltage_port->type = PORT_TYPE_NUMBER;
    voltage_port->unit = "V";
    voltage_port->read_value = read_voltage;
    voltage_port->configure = configure;
    ports[(*ports_len)++] = voltage_port;

    port_t *temperature_port = port_new();
    temperature_port->slot = -1;
    temperature_port->type = PORT_TYPE_NUMBER;
    temperature_port->unit = "C";
    temperature_port->read_value = read_temperature;
    temperature_port->configure = configure;
    ports[(*ports_len)++] = temperature_port;
}
