
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

#include "espgoodies/common.h"
#include "espgoodies/utils.h"

#include "uart.h"

#define REG_UART_BASE(i)            (0x60000000 + (i) * 0xF00)

#define UART_FIFO(i)                (REG_UART_BASE(i) + 0x00)
#define UART_INT_ENA(i)             (REG_UART_BASE(i) + 0x0C)
#define UART_INT_CLR(i)             (REG_UART_BASE(i) + 0x10)
#define UART_STATUS(i)              (REG_UART_BASE(i) + 0x1C)
#define UART_CONF0(i)               (REG_UART_BASE(i) + 0x20)

#define UART_RXFIFO_CNT             0x000000FF
#define UART_RXFIFO_CNT_S           0
#define UART_RXFIFO_FULL_INT_CLR    0x00000001
#define UART_RXFIFO_TOUT_INT_CLR    0x00000100
#define UART_RXFIFO_FULL_INT_ENA    0x00000001
#define UART_RXFIFO_TOUT_INT_ENA    0x00000100

#define UART_TXFIFO_CNT             0x000000FF
#define UART_TXFIFO_CNT_S           16

#define UART_PARITY_EN              0x00000002
#define UART_PARITY                 0x00000001

#define UART_STOP_BIT_NUM           3
#define UART_STOP_BIT_NUM_S         4


void uart_setup(uint8 uart, uint32 baud, uint8 parity, uint8 stop_bits) {
    /* baud rate */
    uart_div_modify(uart, UART_CLK_FREQ / baud);

    /* parity */
    CLEAR_PERI_REG_MASK(UART_CONF0(uart), UART_PARITY | UART_PARITY_EN);
    if (parity != UART_PARITY_NONE) {
        SET_PERI_REG_MASK(UART_CONF0(uart), parity | UART_PARITY_EN);
    }

    /* stop bits */
    SET_PERI_REG_BITS(UART_CONF0(uart), UART_STOP_BIT_NUM, stop_bits, UART_STOP_BIT_NUM_S);

    /* set GPIO rx/tx functions */
    if (uart == 0) {
        PIN_FUNC_SELECT(gpio_get_mux(1), FUNC_U0TXD);
        PIN_FUNC_SELECT(gpio_get_mux(3), FUNC_U0RXD);
    }
    else { /* assuming uart == 1 */
        PIN_FUNC_SELECT(gpio_get_mux(2), FUNC_U1TXD_BK);
        /* UART1 has no rx pin */
    }
}

uint16 uart_read(uint8 uart, uint8 *buff, uint16 max_len, uint32 timeout_us) {
    uint16 got = 0;
    uint32 start = system_get_time();
    bool done = FALSE;

    CLEAR_PERI_REG_MASK(UART_INT_ENA(uart), UART_RXFIFO_FULL_INT_ENA|UART_RXFIFO_TOUT_INT_ENA);

    while ((system_get_time() - start < timeout_us) && !done) {
        while (READ_PERI_REG(UART_STATUS(uart)) & (UART_RXFIFO_CNT << UART_RXFIFO_CNT_S)) {
            buff[got++] = READ_PERI_REG(UART_FIFO(uart)) & 0xFF;
            if (got == max_len) {
                done = TRUE;
                break;
            }
        }
        WRITE_PERI_REG(UART_INT_CLR(uart), UART_RXFIFO_FULL_INT_CLR | UART_RXFIFO_TOUT_INT_CLR);
    }

    SET_PERI_REG_MASK(UART_INT_ENA(uart), UART_RXFIFO_FULL_INT_ENA|UART_RXFIFO_TOUT_INT_ENA);

    return got;
}

uint16 uart_write(uint8 uart, uint8 *buff, uint16 len, uint32 timeout_us) {
    uint32 fifo_count, start = system_get_time();
    uint16 i, write_len = 0;

    for (i = 0; i < len; i++) {
        while (TRUE) {
            fifo_count = READ_PERI_REG(UART_STATUS(uart)) & (UART_TXFIFO_CNT << UART_TXFIFO_CNT_S);
            if (fifo_count < 126) {
                break; /* enough room for another byte */
            }
            if (system_get_time() - start > timeout_us) {
                return write_len; /* timeout */
            }
        }

        WRITE_PERI_REG(UART_FIFO(uart), buff[i]);
        write_len++;
    }

    return write_len;
}

void uart_write_char(uint8 uart, char c) {
    uint32 fifo_count;

    while (TRUE) {
        fifo_count = READ_PERI_REG(UART_STATUS(uart)) & (UART_TXFIFO_CNT << UART_TXFIFO_CNT_S);
        if (fifo_count < 126) {
            break; /* enough room for another byte */
        }
    }

    WRITE_PERI_REG(UART_FIFO(uart), c);
}
