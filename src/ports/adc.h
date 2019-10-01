
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

#ifndef _PORTS_ADC_H
#define _PORTS_ADC_H

#include <c_types.h>

#include "ports.h"


#if defined(HAS_ADC0)
#define HAS_ADC
#endif


#ifdef _DEBUG_ADC
#define DEBUG_ADC(port, f, ...) DEBUG("[%-14s] " f, (port)->id, ##__VA_ARGS__)
#else
#define DEBUG_ADC(...)          {}
#endif


#ifdef HAS_ADC0
extern port_t *                 adc0;
#endif


ICACHE_FLASH_ATTR void          adc_init_ports(void);


#endif  /* _PORTS_ADC_H */
