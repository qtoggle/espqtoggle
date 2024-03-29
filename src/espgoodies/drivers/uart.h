
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

#ifndef _ESPGOODIES_UART_H
#define _ESPGOODIES_UART_H


#include <c_types.h>


#define UART0             0
#define UART1             1

#define UART_PARITY_NONE  2
#define UART_PARITY_ODD   1
#define UART_PARITY_EVEN  0

#define UART_STOP_BITS_1  1
#define UART_STOP_BITS_15 2
#define UART_STOP_BITS_2  3


#ifdef _DEBUG_UART
#define DEBUG_UART(uart, fmt, ...) DEBUG("[uart%-10d] " fmt, uart, ##__VA_ARGS__)
#else
#define DEBUG_UART(...)            {}
#endif


void   ICACHE_FLASH_ATTR uart_setup(uint8 uart_no, uint32 baud, uint8 parity, uint8 stop_bits, bool alt);
uint16 ICACHE_FLASH_ATTR uart_read(uint8 uart_no, uint8 *buff, uint16 max_len, uint32 timeout_us);
uint16 ICACHE_FLASH_ATTR uart_write(uint8 uart_no, uint8 *buff, uint16 len, uint32 timeout_us);
void   ICACHE_FLASH_ATTR uart_write_char(uint8 uart_no, char c);

void   ICACHE_FLASH_ATTR uart_buff_setup(uint8 uart_no, uint16 size);
uint16 ICACHE_FLASH_ATTR uart_buff_peek(uint8 uart_no, uint8 *data, uint16 max_len);
uint16 ICACHE_FLASH_ATTR uart_buff_avail(uint8 uart_no);
uint16 ICACHE_FLASH_ATTR uart_buff_read(uint8 uart_no, uint8 *data, uint16 max_len);
void   ICACHE_FLASH_ATTR uart_buff_cleanup(uint8 uart_no);


#endif /* _ESPGOODIES_UART_H */
