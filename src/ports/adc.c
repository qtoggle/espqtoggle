
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

#include "espgoodies/common.h"
#include "espgoodies/utils.h"

#include "common.h"
#include "ports/adc.h"


#ifdef HAS_ADC


#define ADC0_SLOT                   17

#define ADC_MIN                     0       /* mV */
#define ADC_MAX                     1000    /* mV */

#define ADC_MIN_SAMP_INT            100     /* ms */
#define ADC_DEF_SAMP_INT            1000    /* ms */


ICACHE_FLASH_ATTR static double     read_value(port_t *port);


#ifdef HAS_ADC0
static port_t _adc0 = {
    .slot = ADC0_SLOT,

    .id = ADC0_ID,
    .type = PORT_TYPE_NUMBER,
    .unit = "mV",

    .min = ADC_MIN,
    .max = ADC_MAX,
    .step = UNDEFINED,
    .integer = TRUE,

    .min_sampling_interval = ADC_MIN_SAMP_INT,
    .def_sampling_interval = ADC_DEF_SAMP_INT,

    .read_value = read_value
};

port_t *adc0 = &_adc0;
#endif


double read_value(port_t *port) {
    return system_adc_read() * 1000 / 1024;
}

void adc_init_ports(void) {
#ifdef HAS_ADC0
    port_register(adc0);
#endif
}


#endif  /* HAS_ADC */
