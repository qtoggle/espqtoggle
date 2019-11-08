
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
#include <string.h>
#include <limits.h>
#include <mem.h>
#include <user_interface.h>
#include <gpio.h>

#include "espgoodies/common.h"
#include "espgoodies/system.h"
#include "espgoodies/utils.h"

#include "events.h"
#include "api.h"
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
ICACHE_FLASH_ATTR static void           configure_temperature(port_t *port);
ICACHE_FLASH_ATTR static void           heart_beat_temperature(port_t *port);

#if defined(HAS_DHT0H) || defined(HAS_DHT1H) || defined(HAS_DHT2H)
ICACHE_FLASH_ATTR static double         read_humidity(port_t *port);
ICACHE_FLASH_ATTR static void           configure_humidity(port_t *port);
#endif

ICACHE_FLASH_ATTR static int            attr_get_model(port_t *port, attrdef_t *attrdef);
ICACHE_FLASH_ATTR static void           attr_set_model(port_t *port, attrdef_t *attrdef, int value);

ICACHE_FLASH_ATTR static int            attr_get_gpio(port_t *port, attrdef_t *attrdef);
ICACHE_FLASH_ATTR static void           attr_set_gpio(port_t *port, attrdef_t *attrdef, int index);

ICACHE_FLASH_ATTR static int            attr_get_retries(port_t *port, attrdef_t *attrdef);
ICACHE_FLASH_ATTR static void           attr_set_retries(port_t *port, attrdef_t *attrdef, int value);

#if defined(_DEBUG) && defined(_DEBUG_DHT)
ICACHE_FLASH_ATTR static char *         get_model_str(port_t *port);
#endif

ICACHE_FLASH_ATTR static bool           data_valid(port_t *port, uint64 data);
ICACHE_FLASH_ATTR static int            wait_pulse(port_t *port, bool level);
ICACHE_FLASH_ATTR static uint64         read_data(port_t *port);

ICACHE_FLASH_ATTR static void           input_wire(port_t *port);
ICACHE_FLASH_ATTR static bool           read_wire(port_t *port);
ICACHE_FLASH_ATTR static void           write_wire(port_t *port, bool value);


static char                           * model_choices[] = {"dht11:DHT11", "dht22:DHT22", NULL};

static attrdef_t model_attrdef = {

    .name = "model",
    .display_name = "Model",
    .description = "The sensor model.",
    .type = ATTR_TYPE_STRING,
    .choices = model_choices,
    .modifiable = TRUE,
    .set = attr_set_model,
    .get = attr_get_model

};

static attrdef_t gpio_attrdef = {

    .name = "gpio",
    .display_name = "GPIO Number",
    .description = "The GPIO where the sensor is attached.",
    .type = ATTR_TYPE_NUMBER,
    .choices = all_gpio_choices,
    .modifiable = TRUE,
    .set = attr_set_gpio,
    .get = attr_get_gpio

};

static attrdef_t retries_attrdef = {

    .name = "retries",
    .display_name = "Retries",
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

#ifdef HAS_DHT0T

static extra_info_t dht0_extra_info = {

    .last_data = -1

};

static port_t _dht0_t = {

    .slot = PORT_SLOT_AUTO,

    .id = DHT0T_ID,
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

port_t *dht0_t = &_dht0_t;

#ifdef HAS_DHT0H

static port_t _dht0_h = {

    .slot = PORT_SLOT_AUTO,

    .id = DHT0H_ID,
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

port_t *dht0_h = &_dht0_h;

#endif /* HAS_DHT0H */

#endif /* HAS_DHT0T */

#ifdef HAS_DHT1T

static extra_info_t dht1_extra_info = {

    .last_data = -1

};

static port_t _dht1_t = {

    .slot = PORT_SLOT_AUTO,

    .id = DHT1T_ID,
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

port_t *dht1_t = &_dht1_t;

#ifdef HAS_DHT1H

static port_t _dht1_h = {

    .slot = PORT_SLOT_AUTO,

    .id = DHT1H_ID,
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

port_t *dht1_h = &_dht1_h;

#endif /* HAS_DHT1H */

#endif /* HAS_DHT1T */

#ifdef HAS_DHT2T

static extra_info_t dht2_extra_info = {

    .last_data = -1

};

static port_t _dht2_t = {

    .slot = PORT_SLOT_AUTO,

    .id = DHT2T_ID,
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

port_t *dht2_t = &_dht2_t;

#ifdef HAS_DHT2H

static port_t _dht2_h = {

    .slot = PORT_SLOT_AUTO,

    .id = DHT2H_ID,
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

port_t *dht2_h = &_dht2_h;

#endif /* HAS_DHT2H */

#endif /* HAS_DHT2T */


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

#if defined(HAS_DHT0H) || defined(HAS_DHT1H) || defined(HAS_DHT2H)

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

#endif  /* HAS_DHT0H || HAS_DHT1H || HAS_DHT2H */

int attr_get_model(port_t *port, attrdef_t *attrdef) {
    uint8 value;

    /* read from persisted data */
    memcpy(&value, port->extra_data + MODEL_CONFIG_OFFS, 1);

    /* update cached value */
    set_model(port, get_choice_value_num(attrdef->choices[value]));

    return value;
}

void attr_set_model(port_t *port, attrdef_t *attrdef, int index) {
    uint8 value = index;

    /* update cached value */
    set_model(port, get_choice_value_num(attrdef->choices[value]));

    /* write to persisted data */
    memcpy(port->extra_data + MODEL_CONFIG_OFFS, &value, 1);
}

int attr_get_gpio(port_t *port, attrdef_t *attrdef) {
    uint8 value;

    /* read from persisted data */
    memcpy(&value, port->extra_data + GPIO_CONFIG_OFFS, 1);

    /* update cached value */
    set_gpio(port, get_choice_value_num(attrdef->choices[value]));

    return value;
}

void attr_set_gpio(port_t *port, attrdef_t *attrdef, int index) {
    uint8 value = index;

    /* update cached value */
    set_gpio(port, get_choice_value_num(attrdef->choices[value]));

    /* write to persisted data */
    memcpy(port->extra_data + GPIO_CONFIG_OFFS, &value, 1);
}

int attr_get_retries(port_t *port, attrdef_t *attrdef) {
    uint8 value;

    /* read from persisted data */
    memcpy(&value, port->extra_data + RETRIES_CONFIG_OFFS, 1);

    /* update cached value */
    set_retries(port, value);

    return value;
}

void attr_set_retries(port_t *port, attrdef_t *attrdef, int value) {
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
    gpio_set_pull(get_gpio(port), TRUE);
}

bool read_wire(port_t *port) {
    return !!GPIO_INPUT_GET(get_gpio(port));
}

void write_wire(port_t *port, bool value) {
    gpio_set_pull(get_gpio(port), FALSE);
    GPIO_OUTPUT_SET(get_gpio(port), value);
}


void dht_init_ports(void) {
#ifdef HAS_DHT0T
    port_register(dht0_t);
    dht0_extra_info.temperature_port = dht0_t;

#ifdef HAS_DHT0H
    port_register(dht0_h);
    dht0_extra_info.humidity_port = dht0_h;
#endif
#endif

#ifdef HAS_DHT1T
    port_register(dht1_t);
    dht1_extra_info.temperature_port = dht1_t;

#ifdef HAS_DHT1H
    port_register(dht1_h);
    dht1_extra_info.humidity_port = dht1_h;
#endif
#endif

#ifdef HAS_DHT2T
    port_register(dht2_t);
    dht2_extra_info.temperature_port = dht2_t;

#ifdef HAS_DHT2H
    port_register(dht2_h);
    dht2_extra_info.humidity_port = dht2_h;
#endif
#endif
}


#endif  /* HAS_DHT */
