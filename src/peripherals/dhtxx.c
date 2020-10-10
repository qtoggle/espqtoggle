
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
#include <limits.h>
#include <user_interface.h>

#include "espgoodies/common.h"
#include "espgoodies/drivers/gpio.h"
#include "espgoodies/system.h"

#include "common.h"
#include "peripherals.h"
#include "ports.h"

#include "peripherals/dhtxx.h"


#define PARAM_NO_PIN   0
#define PARAM_NO_MODEL 1

#define MODEL_DHT11    0
#define MODEL_DHT22    1

#define MIN_SAMP_INT   2000    /* Milliseconds */
#define DEF_SAMP_INT   2000    /* Milliseconds */
#define MAX_SAMP_INT   3600000 /* Milliseconds */

#define MIN_TEMP       -40     /* Degrees C */
#define MAX_TEMP       80      /* Degrees C */


typedef struct {

    uint8  pin_no;
    uint8  model;

    uint64 last_data;
    int64  last_read_time_ms;

} user_data_t;


static uint8  ICACHE_FLASH_ATTR wait_pulse(uint8 pin_no, bool level);
static bool   ICACHE_FLASH_ATTR data_valid(uint64 data);
static uint64 ICACHE_FLASH_ATTR read_data(peripheral_t *peripheral);
static uint64 ICACHE_FLASH_ATTR read_data_if_needed(port_t *port);

static double ICACHE_FLASH_ATTR read_temperature_value(port_t *port);
static double ICACHE_FLASH_ATTR read_humidity_value(port_t *port);

static void   ICACHE_FLASH_ATTR init(peripheral_t *peripheral);
static void   ICACHE_FLASH_ATTR make_ports(peripheral_t *peripheral, port_t **ports, uint8 *ports_len);


peripheral_type_t peripheral_type_dhtxx = {

    .init = init,
    .make_ports = make_ports

};


uint8 wait_pulse(uint8 pin_no, bool level) {
    int count;

    for (count = 0; count < 100; count++) {
        if (level != gpio_read_value(pin_no)) {
            return count;
        }

        os_delay_us(1);
    }

    return 0;
}

bool data_valid(uint64 data) {
    uint8 sum = ((data >> 32) & 0xFF) +
                ((data >> 24) & 0xFF) +
                ((data >> 16) & 0xFF) +
                ((data >>  8) & 0xFF);

    return sum == (data & 0xFF);
}

uint64 read_data(peripheral_t *peripheral) {
    int data_bits;
    int width;
    uint64 data = 0;
    user_data_t *user_data = peripheral->user_data;
    uint8 pin_no = user_data->pin_no;

    DEBUG_DHTXX(peripheral, "measurement started");

    /* Wait 10ms before starting the sequence */
    gpio_write_value(pin_no, TRUE);
    os_delay_us(10000);

    /* Temporarily disable interrupts */
    ETS_INTR_LOCK();
    system_soft_wdt_feed();

    /* Hold the wire down for 20ms */
    gpio_write_value(pin_no, FALSE);
    os_delay_us(20000);

    /* Hold the wire up for 40us */
    gpio_write_value(pin_no, TRUE);
    os_delay_us(40);

    /* Switch to reading mode */
    gpio_configure_input(pin_no, TRUE);
    os_delay_us(10);

    /* Sensor is expected to hold the line low by now */
    if (gpio_read_value(pin_no)) {
        DEBUG_DHTXX(peripheral, "timeout waiting for sensor low pulse start");
        ETS_INTR_UNLOCK();
        return -1;
    }

    if (!(width = wait_pulse(pin_no, FALSE))) {
        DEBUG_DHTXX(peripheral, "timeout waiting for sensor high pulse start");
        ETS_INTR_UNLOCK();
        return -1;
    }

    if (!(width = wait_pulse(pin_no, TRUE))) {
        DEBUG_DHTXX(peripheral, "timeout waiting for sensor bit low pulse start");
        ETS_INTR_UNLOCK();
        return -1;
    }

    /* os_delay_us() is not as precise as we would need it to be; we compute a delay ratio based on the expected 80us
     * initial sensor pulse */
    int bit1_threshold = 60 * width / 80;

    for (data_bits = 0; data_bits < 40; data_bits++) {
        if (!(width = wait_pulse(pin_no, FALSE))) {
            DEBUG_DHTXX(peripheral, "timeout waiting for sensor bit high pulse start");
            ETS_INTR_UNLOCK();
            return -1;
        }

        if (!(width = wait_pulse(pin_no, TRUE))) {
            DEBUG_DHTXX(peripheral, "timeout waiting for sensor bit low pulse start");
            ETS_INTR_UNLOCK();
            return -1;
        }

        data <<= 1;
        if (width >= bit1_threshold) {
            data++;
        }
    }

    ETS_INTR_UNLOCK();

    DEBUG_DHTXX(peripheral, "measurement done");

    return data;
}

uint64 read_data_if_needed(port_t *port) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;

    uint64 now_ms = system_uptime_ms();
    if (now_ms - user_data->last_read_time_ms > port->sampling_interval) {
        user_data->last_read_time_ms = now_ms;
        user_data->last_data = read_data(peripheral);
    }

    return user_data->last_data;
}

double read_temperature_value(port_t *port) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;

    uint64 data = read_data_if_needed(port);
    if (!data_valid(data)) {
        return UNDEFINED;
    }

    switch (user_data->model) {
        case MODEL_DHT11:
            return ((data >> 16) & 0xFF);

        case MODEL_DHT22: {
            int32 temp = (data >> 8) & 0xFFFF;
            if (temp & 0x8000) { /* Sign bit */
                temp = -(temp & 0x7FFF);
            }
            return temp / 10.0;
        }
    }

    return UNDEFINED;
}

double read_humidity_value(port_t *port) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;

    uint64 data = read_data_if_needed(port);
    if (!data_valid(data)) {
        return UNDEFINED;
    }

    switch (user_data->model) {
        case MODEL_DHT11:
            return ((data >> 32) & 0xFF);

        case MODEL_DHT22:
            return ((data >> 24) & 0xFFFF) / 10.0;
    }

    return UNDEFINED;
}

void init(peripheral_t *peripheral) {
    user_data_t *user_data = zalloc(sizeof(user_data_t));

    user_data->pin_no = PERIPHERAL_PARAM_UINT8(peripheral, PARAM_NO_PIN);
    user_data->model = PERIPHERAL_PARAM_UINT8(peripheral, PARAM_NO_MODEL);
    user_data->last_data = 0;
    user_data->last_read_time_ms = -LLONG_MAX;

    peripheral->user_data = user_data;

    DEBUG_DHTXX(peripheral, "using GPIO %d", user_data->pin_no);

#if defined(_DEBUG) && defined(_DEBUG_DHTXX)
    switch (user_data->model) {
        case MODEL_DHT11:
            DEBUG_DHTXX(peripheral, "using model %s", "DHT11");

        case MODEL_DHT22:
            DEBUG_DHTXX(peripheral, "using model %s", "DHT22");
    }
#endif
}

void make_ports(peripheral_t *peripheral, port_t **ports, uint8 *ports_len) {
    port_t *temperature_port = port_new();
    temperature_port->slot = -1;
    temperature_port->type = PORT_TYPE_NUMBER;
    temperature_port->min = MIN_TEMP;
    temperature_port->max = MAX_TEMP;
    temperature_port->unit = "C";
    temperature_port->min_sampling_interval = MIN_SAMP_INT;
    temperature_port->max_sampling_interval = MAX_SAMP_INT;
    temperature_port->def_sampling_interval = DEF_SAMP_INT;
    temperature_port->read_value = read_temperature_value;
    ports[(*ports_len)++] = temperature_port;

    port_t *humidity_port = port_new();
    humidity_port->slot = -1;
    humidity_port->type = PORT_TYPE_NUMBER;
    humidity_port->min = 0;
    humidity_port->max = 100;
    humidity_port->unit = "%";
    humidity_port->min_sampling_interval = MIN_SAMP_INT;
    humidity_port->max_sampling_interval = MAX_SAMP_INT;
    humidity_port->def_sampling_interval = DEF_SAMP_INT;
    humidity_port->read_value = read_humidity_value;
    ports[(*ports_len)++] = humidity_port;
}
