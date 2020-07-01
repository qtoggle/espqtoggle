
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

#ifndef _PERIPHERALS_PWMP_H
#define _PERIPHERALS_PWMP_H

#include <c_types.h>

#include "peripherals.h"


#ifdef _DEBUG_PWMP
#define DEBUG_PWMP           DEBUG_PERIPHERAL
#define DEBUG_PWMP_PORT      DEBUG_PORT
#else
#define DEBUG_PWMP(...)      {}
#define DEBUG_PWMP_PORT(...) {}
#endif


extern peripheral_type_t peripheral_type_pwmp;


#endif /* _PERIPHERALS_PWMP_H */
