
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


int battery_get_voltage(void) {
    int v = system_adc_read();

    DEBUG_BATTERY("raw ADC value: %d", v);

    v = v * adc_factor;  /* in millivolts */

    DEBUG_BATTERY("voltage: %d mV", v);

    return v;
}

int battery_get_level(void) {
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
