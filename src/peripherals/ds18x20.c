
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

#include "espgoodies/common.h"
#include "espgoodies/drivers/onewire.h"
#include "espgoodies/utils.h"

#include "common.h"
#include "peripherals.h"
#include "ports.h"

#include "peripherals/ds18x20.h"


#define PARAM_NO_PIN   0

#define MODEL_DS18S20  0x10
#define MODEL_DS18B20  0x28
#define MODEL_DS1822   0x22
#define MODEL_DS1825   0x25
#define MODEL_DS28EA00 0x42

#define ERROR_VALUE    85

#define MIN_SAMP_INT   1000    /* Milliseconds */
#define DEF_SAMP_INT   1000    /* Milliseconds */
#define MAX_SAMP_INT   3600000 /* Milliseconds */

#define MIN_TEMP       -55     /* Degrees C */
#define MAX_TEMP       125    /* Degrees C */


typedef struct {

    one_wire_t *one_wire;

} user_data_t;


static bool   ICACHE_FLASH_ATTR  valid_address(uint8 *addr);
static bool   ICACHE_FLASH_ATTR  valid_family(uint8 *addr);
#if defined(_DEBUG) && defined(_DEBUG_DS18X20)
static char   ICACHE_FLASH_ATTR *get_model_str(uint8 *addr);
#endif

static void   ICACHE_FLASH_ATTR  configure(port_t *port, bool enabled);
static double ICACHE_FLASH_ATTR  read_value(port_t *port);

static void   ICACHE_FLASH_ATTR  init(peripheral_t *peripheral);
static void   ICACHE_FLASH_ATTR  cleanup(peripheral_t *peripheral);
static void   ICACHE_FLASH_ATTR  make_ports(peripheral_t *peripheral, port_t **ports, uint8 *ports_len);


peripheral_type_t peripheral_type_ds18x20 = {

    .init = init,
    .cleanup = cleanup,
    .make_ports = make_ports

};


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

#if defined(_DEBUG) && defined(_DEBUG_DS18X20)
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

void configure(port_t *port, bool enabled) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;

    if (enabled) {
        uint8 addr[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        one_wire_setup(user_data->one_wire);

        DEBUG_DS18X20(peripheral, "searching for sensor");
        one_wire_search_reset(user_data->one_wire);
        while (one_wire_search(user_data->one_wire, addr)) {
            DEBUG_DS18X20(
                peripheral,
                "found sensor at %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                addr[0],
                addr[1],
                addr[2],
                addr[3],
                addr[4],
                addr[5],
                addr[6],
                addr[7]
            );

            if (valid_address(addr)) {
                DEBUG_DS18X20(peripheral, "address is valid");
                if (valid_family(addr)) {
                    DEBUG_DS18X20(peripheral, "sensor model is %s", get_model_str(addr));
                    break;
                }
                else {
                    DEBUG_DS18X20(peripheral, "unknown sensor family");
                }
            }
        }

        if (!addr[0]) {
            DEBUG_DS18X20(peripheral, "no sensor found");
            one_wire_search_reset(user_data->one_wire);
        }
    }
    else {
        one_wire_search_reset(user_data->one_wire);
    }
}

double read_value(port_t *port) {
    peripheral_t *peripheral = port->peripheral;
    user_data_t *user_data = peripheral->user_data;
    one_wire_t *one_wire = user_data->one_wire;

    if (!one_wire->rom[0]) {
        return UNDEFINED;
    }

    one_wire_reset(one_wire);
    one_wire_write(one_wire, ONE_WIRE_CMD_SKIP_ROM, /* parasitic = */ FALSE);
    one_wire_write(one_wire, ONE_WIRE_CMD_CONVERT_T, /* parasitic = */ FALSE);

    os_delay_us(750);
    one_wire_reset(one_wire);
    one_wire_write(one_wire, ONE_WIRE_CMD_SKIP_ROM, /* parasitic = */ FALSE);
    one_wire_write(one_wire, ONE_WIRE_CMD_READ_SCRATCHPAD, /* parasitic = */ FALSE);

    uint8 i, data[9];
    for (i = 0; i < 9; i++) {
        data[i] = one_wire_read(one_wire);
    }

    if (one_wire_crc8(data, 8) != data[8]) {
        DEBUG_DS18X20(peripheral, "invalid CRC while reading scratch pad");
        return UNDEFINED;
    }

    uint16 value = (data[1] << 8) + data[0];
    double temperature = value * 0.0625; // TODO: use temperature resolution according to each specific model

    temperature = round_to(temperature, 1);

    DEBUG_DS18X20(peripheral, "got temperature: %s", dtostr(temperature, 1));

    if (temperature == ERROR_VALUE) {
        DEBUG_DS18X20(peripheral, "temperature read error");
        return UNDEFINED;
    }

    return temperature;
}

void init(peripheral_t *peripheral) {
    user_data_t *user_data = zalloc(sizeof(user_data_t));

    user_data->one_wire = malloc(sizeof(one_wire_t));
    user_data->one_wire->pin_no = PERIPHERAL_PARAM_UINT8(peripheral, PARAM_NO_PIN);

    peripheral->user_data = user_data;

    DEBUG_DS18X20(peripheral, "using GPIO %d", user_data->one_wire->pin_no);
}

void cleanup(peripheral_t *peripheral) {
    user_data_t *user_data = peripheral->user_data;

    free(user_data->one_wire);
}

void make_ports(peripheral_t *peripheral, port_t **ports, uint8 *ports_len) {
    port_t *port = port_create();

    port->slot = -1;
    port->type = PORT_TYPE_NUMBER;
    port->min = MIN_TEMP;
    port->max = MAX_TEMP;
    port->unit = "C";
    port->min_sampling_interval = MIN_SAMP_INT;
    port->max_sampling_interval = MAX_SAMP_INT;
    port->def_sampling_interval = DEF_SAMP_INT;

    port->configure = configure;
    port->read_value = read_value;

    ports[(*ports_len)++] = port;
}
