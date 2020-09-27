
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

#include <mem.h>
#include <c_types.h>
#include <ets_sys.h>
#include <user_interface.h>

#include "espgoodies/common.h"
#include "espgoodies/drivers/gpio.h"
#include "espgoodies/ringbuffer.h"
#include "espgoodies/system.h"

#include "espgoodies/drivers/uart.h"

#define REG_UART_BASE(i)         (0x60000000 + (i) * 0xF00)

#define UART_FIFO(i)             (REG_UART_BASE(i) + 0x00)
#define UART_INT_ST(i)           (REG_UART_BASE(i) + 0x08)
#define UART_INT_ENA(i)          (REG_UART_BASE(i) + 0x0C)
#define UART_INT_CLR(i)          (REG_UART_BASE(i) + 0x10)
#define UART_STATUS(i)           (REG_UART_BASE(i) + 0x1C)
#define UART_CONF0(i)            (REG_UART_BASE(i) + 0x20)

#define UART_RXFIFO_CNT          0x000000FF
#define UART_RXFIFO_CNT_S        0
#define UART_RXFIFO_FULL_INT_CLR BIT(0)
#define UART_RXFIFO_TOUT_INT_CLR BIT(8)
#define UART_RXFIFO_FULL_INT_ENA BIT(0)
#define UART_RXFIFO_TOUT_INT_ENA BIT(8)
#define UART_RXFIFO_FULL_INT_ST  BIT(0)
#define UART_RXFIFO_TOUT_INT_ST  BIT(8)
#define UART_RXFIFO_LEN(uart_no) ((READ_PERI_REG(UART_STATUS(uart_no)) >> UART_RXFIFO_CNT_S) & UART_RXFIFO_CNT)

#define UART_TXFIFO_CNT          0x000000FF
#define UART_TXFIFO_CNT_S        16

#define UART_PARITY_EN           0x00000002
#define UART_PARITY              0x00000001

#define UART_STOP_BIT_NUM        3
#define UART_STOP_BIT_NUM_S      4


static ring_buffer_t *ring_buffer0 = NULL;
static ring_buffer_t *ring_buffer1 = NULL;

static bool           interrupt_handler_attached = FALSE;


static void uart_interrupt_handler(void *arg);
static void handle_interrupt(uint8 uart_no);


void uart_setup(uint8 uart_no, uint32 baud, uint8 parity, uint8 stop_bits) {
    /* Baud rate */
    uart_div_modify(uart_no, UART_CLK_FREQ / baud);

    /* Parity */
    CLEAR_PERI_REG_MASK(UART_CONF0(uart_no), UART_PARITY | UART_PARITY_EN);
    if (parity != UART_PARITY_NONE) {
        SET_PERI_REG_MASK(UART_CONF0(uart_no), parity | UART_PARITY_EN);
    }

    /* Stop bits */
    SET_PERI_REG_BITS(UART_CONF0(uart_no), UART_STOP_BIT_NUM, stop_bits, UART_STOP_BIT_NUM_S);

    /* Set GPIO rx/tx functions */
    if (uart_no == 0) {
        PIN_FUNC_SELECT(gpio_get_mux(1), FUNC_U0TXD);
        PIN_FUNC_SELECT(gpio_get_mux(3), FUNC_U0RXD);
    }
    else { /* Assuming uart == 1 */
        PIN_FUNC_SELECT(gpio_get_mux(2), FUNC_U1TXD_BK);
        /* UART1 has no rx pin */
    }

    DEBUG_UART(
        uart_no,
        "baud=%d, parity=%c, stop_bits=%s",
        baud,
        parity == UART_PARITY_NONE ? 'N' : parity == UART_PARITY_EVEN ? 'E' : 'O',
        stop_bits == UART_STOP_BITS_1 ? "1" : stop_bits == UART_STOP_BITS_15 ? "1.5" : "2"
    );
}

uint16 uart_read(uint8 uart_no, uint8 *buff, uint16 max_len, uint32 timeout_us) {
    uint16 got = 0, discarded = 0;
    uint64 start = system_uptime_us();
    uint8 c;

    CLEAR_PERI_REG_MASK(UART_INT_ENA(uart_no), UART_RXFIFO_FULL_INT_ENA | UART_RXFIFO_TOUT_INT_ENA);

    while ((system_uptime_us() - start < timeout_us)) {
        while (UART_RXFIFO_LEN(uart_no)) {
            c = READ_PERI_REG(UART_FIFO(uart_no)) & 0xFF;
            if (got < max_len) {
                buff[got++] = c;
            }
            else { /* Continue reading all available bytes, but discard them */
                discarded++;
            }
        }
        WRITE_PERI_REG(UART_INT_CLR(uart_no), UART_RXFIFO_FULL_INT_CLR | UART_RXFIFO_TOUT_INT_CLR);
    }

    SET_PERI_REG_MASK(UART_INT_ENA(uart_no), UART_RXFIFO_FULL_INT_ENA | UART_RXFIFO_TOUT_INT_ENA);

#if defined(_DEBUG_UART) && defined(_DEBUG)
    uint32 duration = system_uptime_us() - start;
    char *buff_hex_str = malloc(got * 3 + 1); /* Two digits + one space for each byte, plus one NULL terminator */
    char *p = buff_hex_str;
    uint16 i;
    buff_hex_str[0] = 0;
    for (i = 0; i < got; i++) {
        snprintf(p, 4, "%02X ", buff[i]);
        p += 3;
    }
    DEBUG_UART(
        uart_no,
        "read %d/%d/%d bytes in %d/%d us: %s",
        discarded + got,
        got,
        max_len,
        duration,
        timeout_us,
        buff_hex_str
    );
    free(buff_hex_str);
#endif

    return got;
}

uint16 uart_write(uint8 uart_no, uint8 *buff, uint16 len, uint32 timeout_us) {
    uint32 fifo_count;
    uint64 start = system_uptime_us();
    uint16 i, written = 0;
    bool timeout = FALSE;

    for (i = 0; i < len && !timeout; i++) {
        while (TRUE) {
            fifo_count = READ_PERI_REG(UART_STATUS(uart_no)) & (UART_TXFIFO_CNT << UART_TXFIFO_CNT_S);
            if (fifo_count < 126) {
                break; /* Enough room for another byte */
            }
            if (system_uptime_us() - start > timeout_us) { /* Timeout */
                timeout = TRUE;
                break;
            }
        }

        WRITE_PERI_REG(UART_FIFO(uart_no), buff[i]);
        written++;
    }

#if defined(_DEBUG_UART) && defined(_DEBUG)
    uint32 duration = system_uptime_us() - start;
    char *buff_hex_str = malloc(written * 3 + 1); /* Two digits + one space for each byte, plus one NULL terminator */
    char *p = buff_hex_str;
    buff_hex_str[0] = 0;
    for (i = 0; i < written; i++) {
        snprintf(p, 4, "%02X ", buff[i]);
        p += 3;
    }
    DEBUG_UART(uart_no, "wrote %d/%d bytes in %d/%d us: %s", written, len, duration, timeout_us, buff_hex_str);
    free(buff_hex_str);
#endif

    return written;
}

void uart_write_char(uint8 uart_no, char c) {
    uint32 fifo_count;

    while (TRUE) {
        fifo_count = READ_PERI_REG(UART_STATUS(uart_no)) & (UART_TXFIFO_CNT << UART_TXFIFO_CNT_S);
        if (fifo_count < 126) {
            break; /* Enough room for another byte */
        }
    }

    WRITE_PERI_REG(UART_FIFO(uart_no), c);
}

void uart_buff_setup(uint8 uart_no, uint16 size) {
    DEBUG_UART(uart_no, "setting up ring buffer with size = %d", size);

    switch (uart_no) {
        case UART0:
            ring_buffer0 = ring_buffer_new(size);
            break;

        case UART1:
            ring_buffer1 = ring_buffer_new(size);
            break;
    }

    ETS_UART_INTR_DISABLE();

    if (!interrupt_handler_attached) {
        DEBUG_UART(uart_no, "attaching interrupt handler");

        ETS_UART_INTR_ATTACH(uart_interrupt_handler, NULL);
        interrupt_handler_attached = TRUE;
    }

    ETS_UART_INTR_ENABLE();
}

uint16 uart_buff_peek(uint8 uart_no, uint8 *data, uint16 max_len) {
    ring_buffer_t *ring_buffer;

    switch (uart_no) {
        case UART0:
            ring_buffer = ring_buffer0;
            break;

        case UART1:
            ring_buffer = ring_buffer1;
            break;

        default:
            return 0;
    }

    return ring_buffer_peek(ring_buffer, data, max_len);
}

uint16 uart_buff_avail(uint8 uart_no) {
    ring_buffer_t *ring_buffer;

    switch (uart_no) {
        case UART0:
            ring_buffer = ring_buffer0;
            break;

        case UART1:
            ring_buffer = ring_buffer1;
            break;

        default:
            return 0;
    }

    return ring_buffer->avail;
}

uint16 uart_buff_read(uint8 uart_no, uint8 *data, uint16 max_len) {
    ring_buffer_t *ring_buffer;

    switch (uart_no) {
        case UART0:
            ring_buffer = ring_buffer0;
            break;

        case UART1:
            ring_buffer = ring_buffer1;
            break;

        default:
            return 0;
    }

    /* Disable interrupts so that we don't do any write to buffer during read */
    ETS_UART_INTR_DISABLE();
    uint16 len = ring_buffer_read(ring_buffer, data, max_len);
    ETS_UART_INTR_ENABLE();

#if defined(_DEBUG_UART) && defined(_DEBUG)
    char *buff_hex_str = malloc(len * 3 + 1); /* Two digits + one space for each byte, plus one NULL terminator */
    char *p = buff_hex_str;
    uint16 i;
    buff_hex_str[0] = 0;
    for (i = 0; i < len; i++) {
        snprintf(p, 4, "%02X ", data[i]);
        p += 3;
    }
    DEBUG_UART(uart_no, "read %d bytes: %s", len, buff_hex_str);
    free(buff_hex_str);
#endif

    return len;
}

void uart_buff_cleanup(uint8 uart_no) {
    DEBUG_UART(uart_no, "cleaning up ring buffer");

    switch (uart_no) {
        case UART0:
            ring_buffer_free(ring_buffer0);
            ring_buffer0 = NULL;
            break;

        case UART1:
            ring_buffer_free(ring_buffer1);
            ring_buffer1 = NULL;
            break;
    }
}


void uart_interrupt_handler(void *arg) {
    handle_interrupt(UART0);
    handle_interrupt(UART1);
}

void handle_interrupt(uint8 uart_no) {
    ring_buffer_t *ring_buffer = uart_no == UART0 ? ring_buffer0 : ring_buffer1;
    if (!ring_buffer) {
        return; /* Buffered reading not enabled */
    }

    uint32 uart_status = READ_PERI_REG(UART_INT_ST(uart_no));

    if (uart_status & UART_RXFIFO_TOUT_INT_ST) {
        WRITE_PERI_REG(UART_INT_CLR(uart_no), UART_RXFIFO_TOUT_INT_ST);
    }
    else if (uart_status & UART_RXFIFO_FULL_INT_ST) {
        WRITE_PERI_REG(UART_INT_CLR(uart_no), UART_RXFIFO_FULL_INT_ST);
    }
    else {
        return;
    }

    uint16 rxfifo_len = UART_RXFIFO_LEN(uart_no);
    uint8 data[rxfifo_len];
    for (int i = 0; i < rxfifo_len; i++) {
        data[i] = READ_PERI_REG(UART_FIFO(uart_no));
    }

    if (rxfifo_len) {
        ring_buffer_write(ring_buffer, data, rxfifo_len);
    }
}
