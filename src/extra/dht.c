
/*
 * Copyright (c) Calin Crisan
 * This file is part of espQToggle.
 *
 * espQToggle is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>. 
 */

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <mem.h>
#include <gpio.h>
#include <user_interface.h>

#include "espgoodies/common.h"
#include "espgoodies/system.h"
#include "espgoodies/utils.h"

#include "events.h"
#include "ports.h"
#include "extra/dht.h"


#ifdef HAS_DHT


#define MODEL_DHT11                     0
#define MODEL_DHT22                     1

#define MODEL_DHT11_STR                 "DHT11"
#define MODEL_DHT22_STR                 "DHT22"

#define MIN_RETRIES                     0
#define MAX_RETRIES                     10

#define TEMP_MIN                       -50
#define TEMP_MAX                        100

#define RH_MIN                          0
#define RH_MAX                          100

#define MIN_SAMP_INT                    1000    /* milliseconds */
#define DEF_SAMP_INT                    1000    /* milliseconds */
#define MAX_SAMP_INT                    3600000 /* milliseconds */

#define HEART_BEAT_INTERVAL             1000    /* milliseconds */

#define MODEL_CONFIG_OFFS               0x00    /* 1 byte */
#define GPIO_CONFIG_OFFS                0x01    /* 1 byte */
#define RETRIES_CONFIG_OFFS             0x05    /* 1 byte */

#define GPIO_CHOICES_LEN                8


typedef struct {

    uint64                              last_data;

    uint8                               model;
    uint8                               gpio;
    uint8                               retries;

    port_t                            * temperature_port;
    port_t                            * humidity_port;

} extra_info_t;


#define get_last_data(p)                (((extra_info_t *) (p)->extra_info)->last_data)
#define set_last_data(p, v)             {((extra_info_t *) (p)->extra_info)->last_data = last_data;}

#define get_temperature_port(p)         (((extra_info_t *) (p)->extra_info)->temperature_port)
#define get_humidity_port(p)            (((extra_info_t *) (p)->extra_info)->humidity_port)

#define get_model(p)                    (((extra_info_t *) (p)->extra_info)->model)
#define set_model(p, v)                 {((extra_info_t *) (p)->extra_info)->model = v;}

#define get_gpio(p)                     (((extra_info_t *) (p)->extra_info)->gpio)
#define set_gpio(p, v)                  {((extra_info_t *) (p)->extra_info)->gpio = v;}

#define get_retries(p)                  (((extra_info_t *) (p)->extra_info)->retries)
#define set_retries(p, v)               {((extra_info_t *) (p)->extra_info)->retries = v;}


ICACHE_FLASH_ATTR static double         read_temperature(port_t *port);
ICACHE_FLASH_ATTR static double         read_humidity(port_t *port);
ICACHE_FLASH_ATTR static void           configure_temperature(port_t *port);
ICACHE_FLASH_ATTR static void           configure_humidity(port_t *port);
ICACHE_FLASH_ATTR static void           heart_beat_temperature(port_t *port);

ICACHE_FLASH_ATTR static int            attr_get_model(port_t *port);
ICACHE_FLASH_ATTR static void           attr_set_model(port_t *port, int value);

ICACHE_FLASH_ATTR static int            attr_get_gpio(port_t *port);
ICACHE_FLASH_ATTR static void           attr_set_gpio(port_t *port, int index);

ICACHE_FLASH_ATTR static int            attr_get_retries(port_t *port);
ICACHE_FLASH_ATTR static void           attr_set_retries(port_t *port, int value);

#if defined(_DEBUG) && defined(_DEBUG_DHT)
ICACHE_FLASH_ATTR static char *         get_model_str(port_t *port);
#endif

ICACHE_FLASH_ATTR static bool           data_valid(port_t *port, uint64 data);
ICACHE_FLASH_ATTR static int            wait_pulse(port_t *port, bool level);
ICACHE_FLASH_ATTR static uint64         read_data(port_t *port);

ICACHE_FLASH_ATTR static void           input_wire(port_t *port);
ICACHE_FLASH_ATTR static bool           read_wire(port_t *port);
ICACHE_FLASH_ATTR static void           write_wire(port_t *port, bool value);


static char                           * model_choices[] = {"DHT11", "DHT22", NULL};
static uint8                            gpio_mapping[] = {0, 2, 4, 5, 12, 13, 14, 15};
static char                           * gpio_choices[] = {"gpio 0", "gpio 2", "gpio 4", "gpio 5", "gpio 12", "gpio 13",
                                                          "gpio 14", "gpio 15", NULL};

static attrdef_t model_attrdef = {

    .name = "model",
    .description = "The sensor model.",
    .type = ATTR_TYPE_STRING,
    .choices = model_choices,
    .modifiable = TRUE,
    .set = attr_set_model,
    .get = attr_get_model

};

static attrdef_t gpio_attrdef = {

    .name = "gpio",
    .description = "The GPIO where the sensor is attached.",
    .type = ATTR_TYPE_STRING,
    .choices = gpio_choices,
    .modifiable = TRUE,
    .set = attr_set_gpio,
    .get = attr_get_gpio

};

static attrdef_t retries_attrdef = {

    .name = "retries",
    .description = "The number of measurement retries, in case of error.",
    .type = ATTR_TYPE_NUMBER,
    .modifiable = TRUE,
    .min = MIN_RETRIES,
    .max = MAX_RETRIES,
    .integer = TRUE,
    .set = attr_set_retries,
    .get = attr_get_retries

};

static attrdef_t *attrdefs[] = {

    &model_attrdef,
    &gpio_attrdef,
    &retries_attrdef,
    NULL

};

#ifdef HAS_DHT0
static extra_info_t dht0_extra_info = {

    .last_data = -1

};

static port_t _dht0 = {

    .slot = PORT_SLOT_EXTRA0,

    .id = DHT0_ID,
    .type = PORT_TYPE_NUMBER,
    .min = TEMP_MIN,
    .max = TEMP_MAX,
    .step = UNDEFINED,

    .heart_beat_interval = HEART_BEAT_INTERVAL,
    .extra_info = &dht0_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_temperature,
    .configure = configure_temperature,
    .heart_beat = heart_beat_temperature,

    .attrdefs = attrdefs

};

static port_t _dht0_h = {

    .slot = PORT_SLOT_EXTRA1,

    .id = DHT0_ID "h",
    .type = PORT_TYPE_NUMBER,
    .min = RH_MIN,
    .max = RH_MAX,
    .step = UNDEFINED,
    .integer = TRUE,

    .extra_info = &dht0_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_humidity,
    .configure = configure_humidity,

};

port_t *dt0 = &_dht0;
port_t *dht0_h = &_dht0_h;
#endif

#ifdef HAS_DHT1
static extra_info_t dht1_extra_info = {

    .last_data = -1

};

static port_t _dht1 = {

    .slot = PORT_SLOT_EXTRA2,

    .id = DHT1_ID,
    .type = PORT_TYPE_NUMBER,
    .min = TEMP_MIN,
    .max = TEMP_MAX,
    .step = UNDEFINED,

    .heart_beat_interval = HEART_BEAT_INTERVAL,
    .extra_info = &dht1_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_temperature,
    .configure = configure_temperature,
    .heart_beat = heart_beat_temperature,

    .attrdefs = attrdefs

};

static port_t _dht1_h = {

    .slot = PORT_SLOT_EXTRA3,

    .id = DHT1_ID "h",
    .type = PORT_TYPE_NUMBER,
    .min = RH_MIN,
    .max = RH_MAX,
    .step = UNDEFINED,
    .integer = TRUE,

    .extra_info = &dht1_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_humidity,
    .configure = configure_humidity,

};

port_t *dht1 = &_dht1;
port_t *dht1_h = &_dht1_h;
#endif

#ifdef HAS_DHT2
static extra_info_t dht2_extra_info = {

    .last_data = -1

};

static port_t _dht2 = {

    .slot = PORT_SLOT_EXTRA4,

    .id = DHT2_ID,
    .type = PORT_TYPE_NUMBER,
    .min = TEMP_MIN,
    .max = TEMP_MAX,
    .step = UNDEFINED,

    .heart_beat_interval = HEART_BEAT_INTERVAL,
    .extra_info = &dht2_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_temperature,
    .configure = configure_temperature,
    .heart_beat = heart_beat_temperature,

    .attrdefs = attrdefs

};

static port_t _dht2_h = {

    .slot = PORT_SLOT_EXTRA5,

    .id = DHT2_ID "h",
    .type = PORT_TYPE_NUMBER,
    .min = RH_MIN,
    .max = RH_MAX,
    .step = UNDEFINED,
    .integer = TRUE,

    .extra_info = &dht2_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_humidity,
    .configure = configure_humidity,

};

port_t *dht2 = &_dht2;
port_t *dht2_h = &_dht2_h;
#endif


double read_temperature(port_t *port) {
    uint64 last_data = get_last_data(port);

    if (!data_valid(port, last_data)) {
        return UNDEFINED;
    }

    switch (get_model(port)) {
        case MODEL_DHT11:
            return ((last_data >> 16) & 0xFF);

        case MODEL_DHT22: {
            int temp = (last_data >> 8) & 0xFFFF;
            if (temp & 0x8000) { /* sign bit */
                temp = -(temp & 0x7FFF);
            }
            return temp / 10.0;
        }
    }

    return UNDEFINED;
}

double read_humidity(port_t *port) {
    uint64 last_data = get_last_data(port);

    if (!data_valid(port, last_data)) {
        return UNDEFINED;
    }

    switch (get_model(port)) {
        case MODEL_DHT11:
            return ((last_data >> 32) & 0xFF);

        case MODEL_DHT22:
            return ((last_data >> 24) & 0xFFFF) / 10.0;
    }

    return UNDEFINED;
}

void configure_temperature(port_t *port) {
    gpio_select_func(get_gpio(port));

    DEBUG_DHT(port, "using model \"%s\"", get_model_str(port));
    DEBUG_DHT(port, "using retries = %d", get_retries(port));
    DEBUG_DHT(port, "using gpio = %d", get_gpio(port));

    /* temperature and humidity ports are always enabled or disabled together */
    if (IS_ENABLED(port) && !IS_ENABLED(get_humidity_port(port))) {
        port_enable(get_humidity_port(port));
        event_push_port_update(get_humidity_port(port));
    }
    else if (!IS_ENABLED(port) && IS_ENABLED(get_humidity_port(port))) {
        port_disable(get_humidity_port(port));
        event_push_port_update(get_humidity_port(port));
    }
}

void configure_humidity(port_t *port) {
    /* temperature and humidity ports are always enabled or disabled together */
    if (IS_ENABLED(port) && !IS_ENABLED(get_temperature_port(port))) {
        port_enable(get_temperature_port(port));
        event_push_port_update(get_temperature_port(port));
    }
    else if (!IS_ENABLED(port) && IS_ENABLED(get_temperature_port(port))) {
        port_disable(get_temperature_port(port));
        event_push_port_update(get_temperature_port(port));
    }
}

void heart_beat_temperature(port_t *port) {
    uint8 r = get_retries(port) + 1;
    uint64 last_data;

    while (r) {
        last_data = read_data(port);
        set_last_data(port, last_data);

        if (data_valid(port, last_data)) {
            break;
        }
        r--;
        DEBUG_DHT(port, "read failure, retries = %d", r);
    }
}

int attr_get_model(port_t *port) {
    uint8 value;

    /* read from persisted data */
    memcpy(&value, port->extra_data + MODEL_CONFIG_OFFS, 1);

    /* update cached value */
    set_model(port, value);

    return value;
}

void attr_set_model(port_t *port, int value) {
    /* update cached value */
    set_model(port, value);

    /* write to persisted data */
    memcpy(port->extra_data + MODEL_CONFIG_OFFS, &value, 1);
}

int attr_get_gpio(port_t *port) {
    int i;
    uint8 value;

    /* read from persisted data */
    memcpy(&value, port->extra_data + GPIO_CONFIG_OFFS, 1);

    /* update cached value */
    set_gpio(port, value);

    for (i = 0; i < GPIO_CHOICES_LEN; i++) {
        if (gpio_mapping[i] == value) {
            /* return choice index */
            return i;
        }
    }

    return 0;
}

void attr_set_gpio(port_t *port, int index) {
    uint8 value = gpio_mapping[index];

    /* update cached value */
    set_gpio(port, value);

    /* write to persisted data */
    memcpy(port->extra_data + GPIO_CONFIG_OFFS, &value, 1);
}

int attr_get_retries(port_t *port) {
    uint8 value;

    /* read from persisted data */
    memcpy(&value, port->extra_data + RETRIES_CONFIG_OFFS, 1);

    /* update cached value */
    set_retries(port, value);

    return value;
}

void attr_set_retries(port_t *port, int value) {
    /* update cached value */
    set_retries(port, value);

    /* write to persisted data */
    memcpy(port->extra_data + RETRIES_CONFIG_OFFS, &value, 1);
}

#if defined(_DEBUG) && defined(_DEBUG_DHT)

char *get_model_str(port_t *port) {
    switch (port->extra_data[MODEL_CONFIG_OFFS]) {
        default:
        case MODEL_DHT11:
            return MODEL_DHT11_STR;

        case MODEL_DHT22:
            return MODEL_DHT22_STR;

    }
}

#endif

bool data_valid(port_t *port, uint64 data) {
    uint64 last_data = get_last_data(port);

    if (last_data == -1) {
        return FALSE;
    }

    int sum = ((last_data >> 32) & 0xFF) +
              ((last_data >> 24) & 0xFF) +
              ((last_data >> 16) & 0xFF) +
              ((last_data >>  8) & 0xFF);

    int checksum = last_data & 0xFF;

    return (sum & 0xFF) == checksum;
}

int wait_pulse(port_t *port, bool level) {
    int count;

    for (count = 0; count < 100; count++) {
        if (level != read_wire(port)) {
            return count;
        }

        os_delay_us(1);
    }

    return 0;
}

uint64 read_data(port_t *port) {
    int data_bits;
    int width;
    uint64 data = 0;

    DEBUG_DHT(port, "measurement started");

    /* wait 10ms before starting the sequence */
    write_wire(port, 1);
    os_delay_us(10000);

    /* temporarily disable interrupts */
    ETS_INTR_LOCK();
    system_soft_wdt_feed();

    /* hold the wire down for 20ms */
    write_wire(port, 0);
    os_delay_us(20000);

    /* hold the wire up for 40us */
    write_wire(port, 1);
    os_delay_us(40);

    /* switch to reading mode */
    input_wire(port);
    os_delay_us(10);

    /* sensor is expected to hold the line low by now */
    if (read_wire(port)) {
        DEBUG_DHT(port, "timeout waiting for sensor low pulse start");
        ETS_INTR_UNLOCK();
        return -1;
    }

    if (!(width = wait_pulse(port, 0))) {
        DEBUG_DHT(port, "timeout waiting for sensor high pulse start");
        ETS_INTR_UNLOCK();
        return -1;
    }

    if (!(width = wait_pulse(port, 1))) {
        DEBUG_DHT(port, "timeout waiting for sensor bit low pulse start");
        ETS_INTR_UNLOCK();
        return -1;
    }

    /* os_delay_us() is not as precise as we would need it to be;
     * we compute a delay ratio based on the expected 80us initial sensor pulse */
    int bit1_threshold = 60 * width / 80;

    for (data_bits = 0; data_bits < 40; data_bits++) {
        if (!(width = wait_pulse(port, 0))) {
            DEBUG_DHT(port, "timeout waiting for sensor bit high pulse start");
            ETS_INTR_UNLOCK();
            return -1;
        }

        if (!(width = wait_pulse(port, 1))) {
            DEBUG_DHT(port, "timeout waiting for sensor bit low pulse start");
            ETS_INTR_UNLOCK();
            return -1;
        }

        data <<= 1;
        if (width >= bit1_threshold) {
            data++;
        }
    }

    ETS_INTR_UNLOCK();

    DEBUG_DHT(port, "measurement done");

    return data;
}

void input_wire(port_t *port) {
    GPIO_DIS_OUTPUT(get_gpio(port));
    gpio_set_pullup(get_gpio(port), true);
}

bool read_wire(port_t *port) {
    return !!GPIO_INPUT_GET(get_gpio(port));
}

void write_wire(port_t *port, bool value) {
    gpio_set_pullup(get_gpio(port), false);
    GPIO_OUTPUT_SET(get_gpio(port), value);
}


void dht_init_ports(void) {
#ifdef HAS_DHT0
    port_register(dt0);
    port_register(dht0_h);

    dht0_extra_info.temperature_port = dt0;
    dht0_extra_info.humidity_port = dht0_h;
#endif

#ifdef HAS_DHT1
    port_register(dht1);
    port_register(dht1_h);

    dht1_extra_info.temperature_port = dht1;
    dht1_extra_info.humidity_port = dht1_h;
#endif

#ifdef HAS_DHT2
    port_register(dht2);
    port_register(dht2_h);

    dht2_extra_info.temperature_port = dht2;
    dht2_extra_info.humidity_port = dht2_h;
#endif
}


#endif  /* HAS_DHT */
