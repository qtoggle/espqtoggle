
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
 */

#ifndef _UART_H
#define _UART_H


#include <c_types.h>


#define UART0                       0
#define UART1                       1


ICACHE_FLASH_ATTR uint16            uart_read(uint8 uart, uint8 *buff, uint16 max_len, uint32 timeout_us);


#endif /* _UART_H */
