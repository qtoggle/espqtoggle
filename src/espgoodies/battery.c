
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
#include <c_types.h>
#include <mem.h>
#include <user_interface.h>

#include "common.h"
#include "utils.h"
#include "battery.h"


#ifndef BATTERY_DIV_FACTOR
#define BATTERY_DIV_FACTOR      1
#endif

#ifndef BATTERY_VOLT_100
#define BATTERY_VOLT_100        1000
#endif

#ifndef BATTERY_VOLT_80
#define BATTERY_VOLT_80         800
#endif

#ifndef BATTERY_VOLT_60
#define BATTERY_VOLT_60         600
#endif

#ifndef BATTERY_VOLT_40
#define BATTERY_VOLT_40         400
#endif

#ifndef BATTERY_VOLT_20
#define BATTERY_VOLT_20         200
#endif

#ifndef BATTERY_VOLT_0
#define BATTERY_VOLT_0          0
#endif

#define ADC_LUT_LEN             6

static struct {  /* voltage vs. battery charge level lookup table */

    short voltage;
    short level;

} adc_lut[] = {

    {BATTERY_VOLT_100, 100},
    {BATTERY_VOLT_80,  80},
    {BATTERY_VOLT_60,  60},
    {BATTERY_VOLT_40,  40},
    {BATTERY_VOLT_20,  20},
    {BATTERY_VOLT_0,   0}

};

static double adc_factor = 1 / BATTERY_DIV_FACTOR / 1.024;


int battery_get_voltage() {
    int v = system_adc_read();

    DEBUG_BATTERY("raw ADC value: %d", v);

    v = v * adc_factor;  /* in millivolts */

    DEBUG_BATTERY("voltage: %d mV", v);

    return v;
}

int battery_get_level() {
    int i = 0;
    int v1, v2, v = battery_get_voltage();
    int l1, l2;

    while (i < ADC_LUT_LEN && v < adc_lut[i].voltage) {
        i++;
    }

    if (i == 0) {
        v = adc_lut[0].level;
    }
    else if (i >= ADC_LUT_LEN) {
        v = adc_lut[ADC_LUT_LEN - 1].level;
    }
    else {
        v1 = adc_lut[i].voltage;
        v2 = adc_lut[i - 1].voltage;

        l1 = adc_lut[i].level;
        l2 = adc_lut[i - 1].level;

        v = l1 + (v - v1) * (l2 - l1) / (v2 - v1);
    }

    DEBUG_BATTERY("percent: %d %%", v);

    return v;
}
