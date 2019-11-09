
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
 * Inspired from https://github.com/milesburton/Arduino-Temperature-Control-Library/ and
 *               https://github.com/mathew-hall/esp8266-dht/
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
#include "ports.h"
#include "extra/dallastemp.h"


#ifdef HAS_DALLASTEMP


#define MODEL_DS18S20                   0x10
#define MODEL_DS18B20                   0x28
#define MODEL_DS1822                    0x22
#define MODEL_DS1825                    0x25
#define MODEL_DS28EA00                  0x42

#define ERROR_VALUE                     85

#define MIN_SAMP_INT                    1000    /* milliseconds */
#define DEF_SAMP_INT                    1000    /* milliseconds */
#define MAX_SAMP_INT                    3600000 /* milliseconds */

#define HEART_BEAT_INTERVAL             2000    /* milliseconds */

#define GPIO_CONFIG_OFFS                0x00    /* 1 byte */


typedef struct {

    double                              last_value;
    uint8                               gpio;
    one_wire_t                        * one_wire;

} extra_info_t;


#define get_last_value(p)               (((extra_info_t *) (p)->extra_info)->last_value)
#define set_last_value(p, v)            {((extra_info_t *) (p)->extra_info)->last_value = v;}

#define get_gpio(p)                     (((extra_info_t *) (p)->extra_info)->gpio)
#define set_gpio(p, v)                  {((extra_info_t *) (p)->extra_info)->gpio = v;}

#define get_one_wire(p)                 (((extra_info_t *) (p)->extra_info)->one_wire)
#define set_one_wire(p, v)              {((extra_info_t *) (p)->extra_info)->one_wire = v;}


ICACHE_FLASH_ATTR static double         read_value(port_t *port);
ICACHE_FLASH_ATTR static void           configure(port_t *port);
ICACHE_FLASH_ATTR static void           heart_beat(port_t *port);

ICACHE_FLASH_ATTR static int            attr_get_gpio(port_t *port, attrdef_t *attrdef);
ICACHE_FLASH_ATTR static void           attr_set_gpio(port_t *port, attrdef_t *attrdef, int index);

#if defined(_DEBUG) && defined(_DEBUG_DALLASTEMP)
ICACHE_FLASH_ATTR static char *         get_model_str(uint8 *addr);
#endif

ICACHE_FLASH_ATTR static bool           valid_address(uint8 *addr);
ICACHE_FLASH_ATTR static bool           valid_family(uint8 *addr);


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

static attrdef_t *attrdefs[] = {

    &gpio_attrdef,
    NULL

};


#ifdef HAS_DT0
static extra_info_t dt0_extra_info;

static port_t _dt0 = {

    .slot = PORT_SLOT_AUTO,

    .id = DT0_ID,
    .type = PORT_TYPE_NUMBER,
    .step = UNDEFINED,

    .heart_beat_interval = HEART_BEAT_INTERVAL,
    .extra_info = &dt0_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_value,
    .configure = configure,
    .heart_beat = heart_beat,

    .attrdefs = attrdefs

};

port_t *dt0 = &_dt0;
#endif


#ifdef HAS_DT1
static extra_info_t dt1_extra_info;

static port_t _dt1 = {

    .slot = PORT_SLOT_AUTO,

    .id = DT1_ID,
    .type = PORT_TYPE_NUMBER,
    .step = UNDEFINED,

    .heart_beat_interval = HEART_BEAT_INTERVAL,
    .extra_info = &dt1_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_value,
    .configure = configure,
    .heart_beat = heart_beat,

    .attrdefs = attrdefs

};

port_t *dt1 = &_dt1;
#endif


#ifdef HAS_DT2
static extra_info_t dt2_extra_info;

static port_t _dt2 = {

    .slot = PORT_SLOT_AUTO,

    .id = DT2_ID,
    .type = PORT_TYPE_NUMBER,
    .step = UNDEFINED,

    .heart_beat_interval = HEART_BEAT_INTERVAL,
    .extra_info = &dt2_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_value,
    .configure = configure,
    .heart_beat = heart_beat,

    .attrdefs = attrdefs

};

port_t *dt2 = &_dt2;
#endif


#ifdef HAS_DT3
static extra_info_t dt3_extra_info;

static port_t _dt3 = {

    .slot = PORT_SLOT_AUTO,

    .id = DT3_ID,
    .type = PORT_TYPE_NUMBER,
    .step = UNDEFINED,

    .heart_beat_interval = HEART_BEAT_INTERVAL,
    .extra_info = &dt3_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_value,
    .configure = configure,
    .heart_beat = heart_beat,

    .attrdefs = attrdefs

};

port_t *dt3 = &_dt3;
#endif


#ifdef HAS_DT4
static extra_info_t dt4_extra_info;

static port_t _dt4 = {

    .slot = PORT_SLOT_AUTO,

    .id = DT4_ID,
    .type = PORT_TYPE_NUMBER,
    .step = UNDEFINED,

    .heart_beat_interval = HEART_BEAT_INTERVAL,
    .extra_info = &dt4_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_value,
    .configure = configure,
    .heart_beat = heart_beat,

    .attrdefs = attrdefs

};

port_t *dt4 = &_dt4;
#endif


#ifdef HAS_DT5
static extra_info_t dt5_extra_info;

static port_t _dt5 = {

    .slot = PORT_SLOT_AUTO,

    .id = DT5_ID,
    .type = PORT_TYPE_NUMBER,
    .step = UNDEFINED,

    .heart_beat_interval = HEART_BEAT_INTERVAL,
    .extra_info = &dt5_extra_info,

    .min_sampling_interval = MIN_SAMP_INT,
    .def_sampling_interval = DEF_SAMP_INT,
    .max_sampling_interval = MAX_SAMP_INT,

    .read_value = read_value,
    .configure = configure,
    .heart_beat = heart_beat,

    .attrdefs = attrdefs

};

port_t *dt5 = &_dt5;
#endif


double read_value(port_t *port) {
    return get_last_value(port);
}

void configure(port_t *port) {
    one_wire_t *one_wire = get_one_wire(port);
    uint8 addr[8] = {0, 0, 0, 0, 0, 0, 0, 0};

    if (one_wire) {
        free(one_wire);
    }

    one_wire = zalloc(sizeof(one_wire_t));
    one_wire->gpio_no = get_gpio(port);
    one_wire_setup(one_wire);
    set_one_wire(port, one_wire);

    set_last_value(port, UNDEFINED);

    DEBUG_DT(port, "using gpio = %d", get_gpio(port));

    DEBUG_DT(port, "searching for sensor");
    one_wire_search_reset(one_wire);
    while (one_wire_search(one_wire, addr)) {
        DEBUG_DT(port, "found sensor at %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                 addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], addr[6], addr[7]);

        if (valid_address(addr)) {
            DEBUG_DT(port, "address is valid");
            if (valid_family(addr)) {
                DEBUG_DT(port, "sensor model is %s", get_model_str(addr));
            }
        }
    }

    if (!addr[0]) {
        DEBUG_DT(port, "no sensor found");
        set_one_wire(port, NULL);
        free(one_wire);
    }
}

void heart_beat(port_t *port) {
    one_wire_t *one_wire = get_one_wire(port);
    if (!one_wire) {
        return;  /* not properly configured */
    }

    one_wire_reset(one_wire);
    one_wire_write(one_wire, ONE_WIRE_CMD_SKIP_ROM, /* parasitic = */ FALSE);
    one_wire_write(one_wire, ONE_WIRE_CMD_CONVERT_T, /* parasitic = */ FALSE);

    os_delay_us(750);
    one_wire_reset(one_wire);
    one_wire_write(one_wire, ONE_WIRE_CMD_SKIP_ROM, /* parasitic = */ FALSE);
    one_wire_write(one_wire, ONE_WIRE_CMD_READ_SCRATCHPAD, /* parasitic = */ FALSE);

    uint8 i, data[10];
    for (i = 0; i < 9; i++) {
        data[i] = one_wire_read(one_wire);
    }

    if (one_wire_crc8(data, 8) != data[8]) {
        DEBUG_DT(port, "invalid CRC while reading scratch pad");
        return;
    }

    uint16 temp = (data[1] << 8) + data[0];
    double temperature = temp * 0.0625; // TODO this resolution does not apply to all sensor models
    temperature = ((int) (temperature * 10)) / 10.0;

    DEBUG_DT(port, "got temperature: %d.%d", (int) temperature, (int) ((temperature - (int) temperature) * 10));

    if (temperature == ERROR_VALUE) {
        DEBUG_DT(port, "temperature read error");
        return;
    }

    set_last_value(port, temperature);
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

#if defined(_DEBUG) && defined(_DEBUG_DALLASTEMP)

char *get_model_str(uint8 *addr) {
    switch (addr[0]) {
        case MODEL_DS18S20:
            return "DS18S20";

        case MODEL_DS18B20:
            return "DS18B20";

        case MODEL_DS1822:
            return "DS1822";

        case MODEL_DS1825:
            return "DS1825";

        case MODEL_DS28EA00:
            return "DS28EA00";
    }

    return NULL;
}

#endif

bool valid_address(uint8 *addr) {
    return (one_wire_crc8(addr, 7) == addr[7]);
}

bool valid_family(uint8 *addr) {
    switch (addr[0]) {
        case MODEL_DS18S20:
        case MODEL_DS18B20:
        case MODEL_DS1822:
        case MODEL_DS1825:
        case MODEL_DS28EA00:
            return TRUE;

        default:
            return FALSE;
    }
}


void dallastemp_init_ports(void) {
#ifdef HAS_DT0
    port_register(dt0);
#endif
#ifdef HAS_DT1
    port_register(dt1);
#endif
#ifdef HAS_DT2
    port_register(dt2);
#endif
#ifdef HAS_DT3
    port_register(dt3);
#endif
#ifdef HAS_DT4
    port_register(dt4);
#endif
#ifdef HAS_DT5
    port_register(dt5);
#endif
}


#endif  /* HAS_DALLASTEMP */
