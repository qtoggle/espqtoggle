
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

#ifndef _ESPGOODIES_DEBUG_H
#define _ESPGOODIES_DEBUG_H


#include <c_types.h>


#define DEBUG_BAUD         115200
#define DEBUG_UART_DISABLE -1


void ICACHE_FLASH_ATTR debug_uart_setup(int8 uart_no, bool alt);
int8 ICACHE_FLASH_ATTR debug_uart_get_no(void);


#endif /* _ESPGOODIES_DEBUG_H */
