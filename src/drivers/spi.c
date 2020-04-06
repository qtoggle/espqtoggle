
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
 * Inspired from https://github.com/esp8266/Arduino/blob/master/libraries/SPI/SPI.c
 */

#include <stdlib.h>
#include <mem.h>
#include <c_types.h>
#include <eagle_soc.h>

#include "espgoodies/common.h"

#include "spi.h"


#define ESP8266_REG(addr)     * ((volatile uint32 *)(0x60000000 + (addr)))
#define ESP8266_CLOCK           80000000UL

#define SPI1CMD                 ESP8266_REG(0x100)
#define SPI1C                   ESP8266_REG(0x108)
#define SPI1C1                  ESP8266_REG(0x10C)
#define SPI1CLK                 ESP8266_REG(0x118)
#define SPI1U                   ESP8266_REG(0x11C)
#define SPI1U1                  ESP8266_REG(0x120)
#define SPI1P                   ESP8266_REG(0x12C)
#define SPI1W0                  ESP8266_REG(0x140)

#define GPMUX                   ESP8266_REG(0x800)

#define SPIUDUMMY               (1 << 29) /* DUMMY phase */
#define SPIUMOSI                (1 << 27) /* MOSI phase */
#define SPICWBO                 (1 << 26) /* SPI_WR_BIT_ORDER */
#define SPICRBO                 (1 << 25) /* SPI_RD_BIT_ODER */
#define SPIBUSY                 (1 << 18) /* SPI_USR */
#define SPIUSME                 (1 << 7)  /* SPI_CK_OUT_EDGE, SPI master edge (0: falling, 1: rising) */
#define SPIUDUPLEX              (1 << 0)  /* SPI_DOUTDIN */


#define SPILMISO                8
#define SPILMOSI                17
#define SPIMMISO                0x1FF
#define SPIMMOSI                0x1FF

#define MIN_FREQ_REG_VALUE      0x7FFFF020


typedef union {

    uint32_t reg_value;
    struct {
        unsigned reg_l: 6;
        unsigned reg_h: 6;
        unsigned reg_n: 6;
        unsigned reg_pre: 13;
        unsigned reg_equ: 1;
    };

} spi_clock_t;


ICACHE_FLASH_ATTR static uint32 clock_reg_to_freq(spi_clock_t *reg);
ICACHE_FLASH_ATTR static void   set_freq(uint32 freq);
ICACHE_FLASH_ATTR static void   set_clock_divider(uint32 divider);
ICACHE_FLASH_ATTR static void   set_data_bits(uint16 bits);
ICACHE_FLASH_ATTR static void   transfer_aligned(uint8 *in_buff, uint8 *out_buff, uint32 len);
ICACHE_FLASH_ATTR static void   transfer_bytes(uint8 *in_buff, uint8 *out_buff, uint32 len);


void spi_setup(uint8 bit_order, bool cpol, bool cpha, uint32 freq) {
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, 2); /* GPIO12 becomes MISO */
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U, 2); /* GPIO13 becomes MOSI */
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, 2); /* GPIO14 becomes SCLK */
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, 2); /* GPIO15 becomes CS */

    /* Frequency */
    set_freq(freq);

    /* Bit order */
    SPI1C1 = 0;
    if (bit_order == SPI_BIT_ORDER_MSB_FIRST) {
        SPI1C = 0;
    }
    else {
        SPI1C = SPICWBO | SPICRBO;
    }

    /* CPOL/CPHA */
    SPI1U = SPIUMOSI | SPIUDUPLEX;

    if (cpol) {
        cpha = !cpha;
    }

    if (cpha) {
        SPI1U |= SPIUSME;
    }

    if(cpol) {
        SPI1P |= SPIUDUMMY;
    }

    /* Set 8 data bits */
    SPI1U1 = (7 << SPILMOSI) | (7 << SPILMISO);

    DEBUG_SPI("bit_order=%c, cpol=%d, cpha=%d, freq=%dHz",
              bit_order == SPI_BIT_ORDER_MSB_FIRST ? 'M' : 'L', cpol, cpha, freq);
}

void spi_transfer(uint8 *in_buff, uint8 *out_buff, uint32 len) {
    while (len) {
        if (len > 64) {
            transfer_bytes(out_buff, in_buff, 64);
            len -= 64;
            if (out_buff) {
                out_buff += 64;
            }
            if (in_buff) {
                in_buff += 64;
            }
        }
        else {
            transfer_bytes(out_buff, in_buff, len);
            len = 0;
        }
    }

    DEBUG_SPI("transferred %d bytes", len);
}

uint8 spi_transfer_byte(uint8 byte) {
    while (SPI1CMD & SPIBUSY) {}
    SPI1W0 = byte;
    SPI1CMD |= SPIBUSY;
    while (SPI1CMD & SPIBUSY) {}

    return (uint8) (SPI1W0 & 0xFF);
}


uint32 clock_reg_to_freq(spi_clock_t *reg) {
    return (ESP8266_CLOCK / ((reg->reg_pre + 1) * (reg->reg_n + 1)));
}

void set_freq(uint32 freq) {
    if (freq >= ESP8266_CLOCK) {
        set_clock_divider(0x80000000);
        if (freq > ESP8266_CLOCK) {
            DEBUG_SPI("limiting freq to max = %d Hz", ESP8266_CLOCK);
        }
        return;
    }

    spi_clock_t min_freq_reg = {MIN_FREQ_REG_VALUE};

    uint32 min_freq = clock_reg_to_freq(&min_freq_reg);
    if (freq < min_freq) {
        /* Use minimum possible clock */
        DEBUG_SPI("limiting freq to min = %d Hz", min_freq_reg.reg_value);
        set_clock_divider(min_freq_reg.reg_value);
        return;
    }

    uint8 cal_n = 1;
    spi_clock_t best_reg = {0};
    int32 best_freq = 0;

    /* Find the best match */
    while (cal_n <= 0x3F) { /* 0x3F is max for N */
        spi_clock_t reg = {0};
        int32 cal_freq;
        int32 cal_pre;
        int8 cal_pre_vari = -2;

        reg.reg_n = cal_n;

        /* Test different variants for pre (we calculate in int so we miss the decimals, testing is the easiest and
         * fastest way) */
        while (cal_pre_vari++ <= 1) {
            cal_pre = (((ESP8266_CLOCK / (reg.reg_n + 1)) / freq) - 1) + cal_pre_vari;
            if(cal_pre > 0x1FFF) {
                reg.reg_pre = 0x1FFF;
            }
            else if( cal_pre <= 0) {
                reg.reg_pre = 0;
            }
            else {
                reg.reg_pre = cal_pre;
            }

            reg.reg_l = ((reg.reg_n + 1) / 2);

            /* Test calculation */
            cal_freq = clock_reg_to_freq(&reg);
            if (cal_freq == freq) { /* exact match */
                memcpy(&best_reg, &reg, sizeof(best_reg));
                break;
            }
            else if (cal_freq < freq) {
                /* Never go above the requested frequency */
                if (abs(freq - cal_freq) < abs(freq - best_freq)) {
                    best_freq = cal_freq;
                    memcpy(&best_reg, &reg, sizeof(best_reg));
                }
            }
        }
        if (cal_freq == freq) { /* Exact match */
            break;
        }

        cal_n++;
    }

    set_clock_divider(best_reg.reg_value);
}

void set_clock_divider(uint32 divider) {
    DEBUG_SPI("setting clock divider = %d", divider);

    if (divider == 0x80000000) {
        GPMUX |= (1 << 9);
    }
    else {
        GPMUX &= ~(1 << 9);
    }

    SPI1CLK = divider;
}

void set_data_bits(uint16 bits) {
    uint32 mask = ~((SPIMMOSI << SPILMOSI) | (SPIMMISO << SPILMISO));
    bits--;
    SPI1U1 = ((SPI1U1 & mask) | ((bits << SPILMOSI) | (bits << SPILMISO)));
}

void transfer_aligned(uint8 *in_buff, uint8 *out_buff, uint32 len) {
    while(SPI1CMD & SPIBUSY) {}

    set_data_bits(len * 8);

    volatile uint32 *fifo_ptr = &SPI1W0;

    if (out_buff) {
        uint8 out_len = ((len + 3) / 4);
        uint32 *data_ptr = (uint32 *) out_buff;
        while (out_len--) {
            *(fifo_ptr++) = *(data_ptr++);
        }
    }
    else {
        uint8 out_len = ((len + 3) / 4);
        /* no output data, fill with dummy data */
        while (out_len--) {
            *(fifo_ptr++) = 0xFFFFFFFF;
        }
    }

    SPI1CMD |= SPIBUSY;
    while(SPI1CMD & SPIBUSY) {}

    if (in_buff) {
        uint32 *data_ptr = (uint32 *) in_buff;
        fifo_ptr = &SPI1W0;
        uint8 in_len = len;
        /* Unlike out_len above, in_len tracks *bytes* since we must transfer only the requested bytes to avoid
         * overwriting other vars */
        while (in_len >= 4) {
            *(data_ptr++) = *(fifo_ptr++);
            in_len -= 4;
            in_buff += 4;
        }

        volatile uint8 *fifo_ptr_b = (volatile uint8 *) fifo_ptr;
        while (in_len--) {
            *(in_buff++) = *(fifo_ptr_b++);
        }
    }
}

void transfer_bytes(uint8 *in_buff, uint8 *out_buff, uint32 len) {
    if (!((uint32) out_buff & 3) && !((uint32)in_buff & 3)) {
        /* Input and output buffers are both 32 bit aligned or NULL */
        transfer_aligned(out_buff, in_buff, len);
    }
    else {
        /* HW FIFO has 64 bytes limit and transfer_bytes breaks up large transfers into 64 byte chunks before calling
         * this function. We know at this point that at least one direction is misaligned, so use temporary buffer to
         * align everything. No need for separate out and in aligned copies, we can overwrite our out copy with the
         * input data safely */

        uint8 aligned[64]; /* Stack vars are always 32 bit aligned */
        if (out_buff) {
            memcpy(aligned, out_buff, len);
        }
        transfer_aligned(out_buff ? aligned : NULL, in_buff ? aligned : NULL, len);
        if (in_buff) {
            memcpy(in_buff, aligned, len);
        }
    }
}
