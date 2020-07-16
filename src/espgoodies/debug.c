
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

#include <osapi.h>
#include <user_interface.h>

#include "espgoodies/drivers/uart.h"

#include "espgoodies/debug.h"


static int8 debug_uart_no;


static void ICACHE_FLASH_ATTR debug_putc_func(char c);


void debug_uart_setup(int8 uart_no) {
    debug_uart_no = uart_no;

    if (uart_no >= 0) {
        uart_setup(uart_no, DEBUG_BAUD, UART_PARITY_NONE, UART_STOP_BITS_1);
        os_install_putc1(debug_putc_func);
        system_set_os_print(1);
    }
    else {
        system_set_os_print(0);
    }
}

int8 debug_uart_get_no(void) {
    return debug_uart_no;
}

void debug_putc_func(char c) {
    uart_write_char(debug_uart_no, c);
}
