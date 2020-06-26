
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
#include <c_types.h>
#include <mem.h>
#include <user_interface.h>

#include "common.h"
#include "system.h"
#include "utils.h"
#include "battery.h"


static struct {  /* Voltage vs. battery SoC lookup table */

    uint16 voltage;
    uint16 level;

} lut[BATTERY_LUT_LEN] = {

    {0, 100},
    {0,  80},
    {0,  60},
    {0,  40},
    {0,  20},
    {0,   0}

};

static uint16 battery_div_factor = 0;


void battery_init(uint8 *config_data) {
    memcpy(&battery_div_factor, config_data + SYSTEM_CONFIG_OFFS_BATTERY_DIV_FACTOR, 2);
    for (int i = 0; i < BATTERY_LUT_LEN; i++) {
        memcpy(&lut[i].voltage, config_data + SYSTEM_CONFIG_OFFS_BATTERY_VOLTAGES + 2 * i, 2);
    }

    DEBUG_BATTERY("loaded battery: div_factor = %d/1000, "
                  "v0 = %d, v20 = %d, v40 = %d, v60 = %d, v80 = %d, v100 = %d",
                  battery_div_factor, lut[0].voltage, lut[1].voltage, lut[2].voltage,
                  lut[3].voltage, lut[4].voltage, lut[5].voltage);
}

void battery_save(uint8 *config_data) {
    memcpy(config_data + SYSTEM_CONFIG_OFFS_BATTERY_DIV_FACTOR, &battery_div_factor, 2);
    for (int i = 0; i < BATTERY_LUT_LEN; i++) {
        memcpy(config_data + SYSTEM_CONFIG_OFFS_BATTERY_VOLTAGES + 2 * i, &lut[i].voltage, 2);
    }
}

void battery_configure(uint16 div_factor, uint16 voltages[]) {
    battery_div_factor = div_factor;
    for (int i = 0; i < BATTERY_LUT_LEN; i++) {
        lut[i].voltage = voltages[i];
    }

    DEBUG_BATTERY("configuring battery: div_factor = %d/1000, "
                  "v0 = %d, v20 = %d, v40 = %d, v60 = %d, v80 = %d, v100 = %d",
                  div_factor, voltages[0], voltages[1], voltages[2], voltages[3], voltages[4], voltages[5]);
}

void battery_get_config(uint16 *div_factor, uint16 *voltages) {
    *div_factor = battery_div_factor;
    for (int i = 0; i < BATTERY_LUT_LEN; i++) {
        memcpy(voltages + i, &lut[i].voltage, 2);
    }
}

bool battery_enabled(void) {
    return battery_div_factor > 0;
}


uint16 battery_get_voltage(void) {
    int v = system_adc_read();

    DEBUG_BATTERY("raw ADC value: %d", v);

    if (battery_div_factor) {
        v = v * 1024.0 / battery_div_factor;
    }
    else  {
        v = 0;
    }

    /* v is now in millivolts */

    DEBUG_BATTERY("voltage: %d mV", v);

    return v;
}

uint8 battery_get_level(void) {
    int i = 0;
    int v1, v2, v = battery_get_voltage();
    int l1, l2;

    while (i < BATTERY_LUT_LEN && v < lut[i].voltage) {
        i++;
    }

    if (i == 0) {
        v = lut[0].level;
    }
    else if (i >= BATTERY_LUT_LEN) {
        v = lut[BATTERY_LUT_LEN - 1].level;
    }
    else {
        v1 = lut[i].voltage;
        v2 = lut[i - 1].voltage;

        l1 = lut[i].level;
        l2 = lut[i - 1].level;

        v = l1 + (v - v1) * (l2 - l1) / (v2 - v1);
    }

    DEBUG_BATTERY("percent: %d %%", v);

    return v;
}
