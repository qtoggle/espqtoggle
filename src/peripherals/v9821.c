
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

#include "peripherals/v9821.h"


#define MIN_SAMP_INT 1000    /* Milliseconds */
#define DEF_SAMP_INT 5000    /* Milliseconds */
#define MAX_SAMP_INT 3600000 /* Milliseconds */

#define UART_NO        UART0
#define UART_BAUD      9600
#define UART_PARITY    UART_PARITY_EVEN
#define UART_STOP_BITS UART_STOP_BITS_1

#define WRITE_REQ_TIMEOUT 20000 /* Microseconds */
#define WAIT_RESP_TIMEOUT 70000 /* Microseconds */
#define READ_RESP_TIMEOUT 50000 /* Microseconds */
#define REQUEST           {0xFE, 0x01, 0x0F, 0x08, 0x00, 0x00, 0x00, 0x1C}
#define RESPONSE_HEADER   {0xFE, 0x01, 0x08}
#define RESPONSE_LEN      36


typedef struct {

    double  last_active_power;
    double  last_reactive_power;
    double  last_apparent_power;
    double  last_power_factor;
    double  last_energy;
    double  last_current;
    double  last_voltage;
    double  last_frequency;
    int64   last_read_time_ms;
    bool    configured;

    port_t *active_power_port; /* Needed for sampling interval */

} user_data_t;


static double ICACHE_FLASH_ATTR read_active_power(port_t *port);
static double ICACHE_FLASH_ATTR read_reactive_power(port_t *port);
static double ICACHE_FLASH_ATTR read_apparent_power(port_t *port);
static double ICACHE_FLASH_ATTR read_power_factor(port_t *port);
static double ICACHE_FLASH_ATTR read_energy(port_t *port);
static double ICACHE_FLASH_ATTR read_current(port_t *port);
static double ICACHE_FLASH_ATTR read_voltage(port_t *port);
static double ICACHE_FLASH_ATTR read_frequency(port_t *port);

static bool   ICACHE_FLASH_ATTR read_data(peripheral_t *peripheral);
static void   ICACHE_FLASH_ATTR read_data_if_needed(peripheral_t *peripheral);

static void   ICACHE_FLASH_ATTR reset_read_values(user_data_t *user_data);

static void   ICACHE_FLASH_ATTR configure(port_t *port, bool enabled);

static void   ICACHE_FLASH_ATTR init(peripheral_t *peripheral);
static void   ICACHE_FLASH_ATTR make_ports(peripheral_t *peripheral, port_t **ports, uint8 *ports_len);


peripheral_type_t peripheral_type_v9821 = {

    .init = init,
    .make_ports = make_ports

};


double read_active_power(port_t *port) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;

    read_data_if_needed(peripheral);

    return user_data->last_active_power;
}

double read_reactive_power(port_t *port) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;

    read_data_if_needed(peripheral);

    return user_data->last_reactive_power;
}

double read_apparent_power(port_t *port) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;

    read_data_if_needed(peripheral);

    return user_data->last_apparent_power;
}

double read_power_factor(port_t *port) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;

    read_data_if_needed(peripheral);

    return user_data->last_power_factor;
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

double read_frequency(port_t *port) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;

    read_data_if_needed(peripheral);

    return user_data->last_frequency;
}

bool read_data(peripheral_t *peripheral) {
    user_data_t *user_data = peripheral->user_data;

    /* Write request */
    static uint8 request[] = REQUEST;
    uint16 size = uart_write(UART_NO, request, sizeof(request), WRITE_REQ_TIMEOUT);
    if (size != sizeof(request)) {
        DEBUG_V9821(peripheral, "failed to write request: %d/%d bytes written", size, sizeof(request));
        return FALSE;
    }
    DEBUG_V9821(peripheral, "request sent");

    /* Read response */
    static uint8 read_buff[RESPONSE_LEN];
    uint16 i;

    size = uart_read(UART_NO, read_buff, RESPONSE_LEN, WAIT_RESP_TIMEOUT + READ_RESP_TIMEOUT);
    if (size != RESPONSE_LEN) {
        DEBUG_V9821(peripheral, "failed to read response: %d/%d bytes read", size, RESPONSE_LEN);
        return FALSE;
    }

    /* Validate header */
    static uint8 response_header[] = RESPONSE_HEADER;
    if (memcmp(read_buff, response_header, sizeof(response_header))) {
        DEBUG_V9821(peripheral, "failed to read response: unexpected response header");
        return FALSE;
    }

    /* Compute & validate checksum */
    uint8 checksum = 0;
    for (i = 0; i < size - 1; i++) {
        checksum += read_buff[i];
    }

    checksum = ~checksum;
    checksum += 0x33;

    if (checksum != read_buff[size - 1]) {
        DEBUG_V9821(
            peripheral,
            "failed to read response: invalid checksum (expected %02X, got %02X)",
            checksum,
            read_buff[size - 1]
        );
        return FALSE;
    }

    static uint8 *p_value;
    static char hex_value[9];
    static uint32 int_value;

    /* Parse energy */
    p_value = read_buff + 3;
    snprintf(hex_value, sizeof(hex_value), "%02X%02X%02X%02X", p_value[3], p_value[2], p_value[1], p_value[0]);
    int_value = strtol(hex_value, NULL, 10);
    DEBUG_V9821(peripheral, "read energy: %d/100 kWh", int_value);
    user_data->last_energy = int_value / 100.0;

    /* Parse voltage */
    p_value = read_buff + 7;
    snprintf(hex_value, sizeof(hex_value), "%02X%02X%02X%02X", p_value[3], p_value[2], p_value[1], p_value[0]);
    int_value = strtol(hex_value, NULL, 10);
    DEBUG_V9821(peripheral, "read voltage: %d/10 V", int_value);
    user_data->last_voltage = int_value / 10.0;

    /* Parse current */
    p_value = read_buff + 11;
    snprintf(hex_value, sizeof(hex_value), "%02X%02X%02X%02X", p_value[3], p_value[2], p_value[1], p_value[0]);
    int_value = strtol(hex_value, NULL, 10);
    DEBUG_V9821(peripheral, "read current: %d/10000 A", int_value);
    user_data->last_current = int_value / 10000.0;

    /* Parse frequency */
    p_value = read_buff + 15;
    snprintf(hex_value, sizeof(hex_value), "%02X%02X%02X%02X", p_value[3], p_value[2], p_value[1], p_value[0]);
    int_value = strtol(hex_value, NULL, 10);
    DEBUG_V9821(peripheral, "read frequency: %d/100 Hz", int_value);
    user_data->last_frequency = int_value / 100.0;

    /* Parse active power */
    p_value = read_buff + 19;
    snprintf(hex_value, sizeof(hex_value), "%02X%02X%02X%02X", p_value[3], p_value[2], p_value[1], p_value[0]);
    int_value = strtol(hex_value, NULL, 10);
    DEBUG_V9821(peripheral, "read active power: %d/100 W", int_value);
    user_data->last_active_power = int_value / 100.0;

    /* Parse reactive power */
    p_value = read_buff + 23;
    snprintf(hex_value, sizeof(hex_value), "%02X%02X%02X%02X", p_value[3], p_value[2], p_value[1], p_value[0]);
    int_value = strtol(hex_value, NULL, 10);
    DEBUG_V9821(peripheral, "read reactive power: %d/100 W", int_value);
    user_data->last_reactive_power = int_value / 100.0;

    /* Parse apparent power */
    p_value = read_buff + 27;
    snprintf(hex_value, sizeof(hex_value), "%02X%02X%02X%02X", p_value[3], p_value[2], p_value[1], p_value[0]);
    int_value = strtol(hex_value, NULL, 10);
    DEBUG_V9821(peripheral, "read apparent power: %d/100 W", int_value);
    user_data->last_apparent_power = int_value / 100.0;

    /* Parse power factor */
    p_value = read_buff + 31;
    snprintf(hex_value, sizeof(hex_value), "%02X%02X%02X%02X", p_value[3], p_value[2], p_value[1], p_value[0]);
    int_value = strtol(hex_value, NULL, 10);
    DEBUG_V9821(peripheral, "read power factor: %d/10 %%", int_value);
    user_data->last_power_factor = int_value / 10.0;

    return TRUE;
}

void reset_read_values(user_data_t *user_data) {
    user_data->last_active_power = UNDEFINED;
    user_data->last_reactive_power = UNDEFINED;
    user_data->last_apparent_power = UNDEFINED;
    user_data->last_power_factor = UNDEFINED;
    user_data->last_energy = UNDEFINED;
    user_data->last_current = UNDEFINED;
    user_data->last_voltage = UNDEFINED;
    user_data->last_frequency = UNDEFINED;
}

void read_data_if_needed(peripheral_t *peripheral) {
    user_data_t *user_data = peripheral->user_data;
    port_t *active_power_port = user_data->active_power_port;

    /* Use activer_power port's sampling interval for all data */

    uint64 now_ms = system_uptime_ms();
    uint64 delta = now_ms - user_data->last_read_time_ms;
    if (delta >= active_power_port->sampling_interval) {
        user_data->last_read_time_ms = now_ms;
        DEBUG_V9821(peripheral, "data reading is needed");
        if (!read_data(peripheral)) {
            reset_read_values(user_data);
        }
    }
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
            debug_uart_setup(1 - UART_NO);
        }
#endif
        DEBUG_V9821(peripheral, "configuring serial port");
        uart_setup(UART_NO, UART_BAUD, UART_PARITY, UART_STOP_BITS);
        user_data->configured = TRUE;
    }
}

void init(peripheral_t *peripheral) {
    peripheral->user_data = zalloc(sizeof(user_data_t));
}

void make_ports(peripheral_t *peripheral, port_t **ports, uint8 *ports_len) {
    user_data_t *user_data = peripheral->user_data;

    port_t *active_power_port = port_create();
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

    port_t *reactive_power_port = port_create();
    reactive_power_port->slot = -1;
    reactive_power_port->type = PORT_TYPE_NUMBER;
    reactive_power_port->unit = "VA";
    reactive_power_port->read_value = read_reactive_power;
    reactive_power_port->configure = configure;
    ports[(*ports_len)++] = reactive_power_port;

    port_t *apparent_power_port = port_create();
    apparent_power_port->slot = -1;
    apparent_power_port->type = PORT_TYPE_NUMBER;
    apparent_power_port->unit = "VAR";
    apparent_power_port->read_value = read_apparent_power;
    apparent_power_port->configure = configure;
    ports[(*ports_len)++] = apparent_power_port;

    port_t *power_factor_port = port_create();
    power_factor_port->slot = -1;
    power_factor_port->type = PORT_TYPE_NUMBER;
    power_factor_port->unit = "%";
    power_factor_port->read_value = read_power_factor;
    power_factor_port->configure = configure;
    ports[(*ports_len)++] = power_factor_port;

    port_t *energy_port = port_create();
    energy_port->slot = -1;
    energy_port->type = PORT_TYPE_NUMBER;
    energy_port->unit = "kWh";
    energy_port->read_value = read_energy;
    energy_port->configure = configure;
    ports[(*ports_len)++] = energy_port;

    port_t *current_port = port_create();
    current_port->slot = -1;
    current_port->type = PORT_TYPE_NUMBER;
    current_port->unit = "A";
    current_port->read_value = read_current;
    current_port->configure = configure;
    ports[(*ports_len)++] = current_port;

    port_t *voltage_port = port_create();
    voltage_port->slot = -1;
    voltage_port->type = PORT_TYPE_NUMBER;
    voltage_port->unit = "V";
    voltage_port->read_value = read_voltage;
    voltage_port->configure = configure;
    ports[(*ports_len)++] = voltage_port;

    port_t *frequency_port = port_create();
    frequency_port->slot = -1;
    frequency_port->type = PORT_TYPE_NUMBER;
    frequency_port->unit = "Hz";
    frequency_port->read_value = read_frequency;
    frequency_port->configure = configure;
    ports[(*ports_len)++] = frequency_port;
}
