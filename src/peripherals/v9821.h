
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
 * Based on https://github.com/MacWyznawca/HLW8012_BL0937_ESP.
 *
 */

#ifndef _PERIPHERALS_V9821_H
#define _PERIPHERALS_V9821_H

#include <c_types.h>

#include "peripherals.h"


#ifdef _DEBUG_V9821
#define DEBUG_V9821      DEBUG_PERIPHERAL
#else
#define DEBUG_V9821(...) {}
#endif


extern peripheral_type_t peripheral_type_v9821;


#endif /* _PERIPHERALS_V9821_H */
