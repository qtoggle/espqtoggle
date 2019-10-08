
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

#include <c_types.h>
#include <ets_sys.h>
#include <user_interface.h>

#include "uart.h"

#define REG_UART_BASE(i)            (0x60000000 + (i)*0xf00)
#define UART_FIFO(i)                (REG_UART_BASE(i) + 0x0)
#define UART_STATUS(i)              (REG_UART_BASE(i) + 0x1C)

#define UART_RXFIFO_CNT             0x000000FF
#define UART_RXFIFO_CNT_S           0


uint16 uart_read(uint8 uart, uint8 *buff, uint16 max_len, uint32 timeout_us) {
    ETS_UART_INTR_DISABLE();
    uint16 got = 0;
    uint32 start = system_get_time();
    bool done = FALSE;
    while ((system_get_time() - start < timeout_us) && !done) {
        while (READ_PERI_REG(UART_STATUS(uart)) & (UART_RXFIFO_CNT << UART_RXFIFO_CNT_S)) {
            buff[got++] = READ_PERI_REG(UART_FIFO(uart)) & 0xFF;
            if (got == max_len) {
                done = TRUE;
                break;
            }
        }
    }

    ETS_UART_INTR_ENABLE();

    return got;
}
