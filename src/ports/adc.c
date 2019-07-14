
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

#include "espgoodies/common.h"
#include "espgoodies/utils.h"

#include "ports/adc.h"


#ifdef HAS_ADC


#define ADC0_SLOT                   16

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
    .unit = "millivolts",

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
