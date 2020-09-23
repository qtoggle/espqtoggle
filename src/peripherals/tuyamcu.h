
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
 * Based on https://developer.tuya.com/en/docs/iot/device-development/access-mode-mcu/wifi-general-solution/
 *          software-reference-wifi/tuya-cloud-universal-serial-port-access-protocol
 *
 */

#ifndef _PERIPHERALS_TYUA_MCU_H
#define _PERIPHERALS_TUYA_MCU_H

#include <c_types.h>

#include "peripherals.h"


#ifdef _DEBUG_TUYA_MCU
#define DEBUG_TUYA_MCU      DEBUG_PERIPHERAL
#else
#define DEBUG_TUYA_MCU(...) {}
#endif


extern peripheral_type_t peripheral_type_tuya_mcu;


#endif /* _PERIPHERALS_TUYA_MCU_H */
