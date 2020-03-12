
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

#include <stdlib.h>
#include <string.h>
#include <mem.h>
#include <ctype.h>
#include <limits.h>
#include <user_interface.h>
#include <espconn.h>

#include "common.h"
#include "gpio.h"


static uint32                   gpio_configured_state;
static uint32                   gpio_output_state;
static uint32                   gpio_pull_state;


int gpio_get_mux(int gpio_no) {
    switch (gpio_no) {
        case 0:
            return PERIPHS_IO_MUX_GPIO0_U;

        case 1:
            return PERIPHS_IO_MUX_U0TXD_U;

        case 2:
            return PERIPHS_IO_MUX_GPIO2_U;

        case 3:
            return PERIPHS_IO_MUX_U0RXD_U;

        case 4:
            return PERIPHS_IO_MUX_GPIO4_U;

        case 5:
            return PERIPHS_IO_MUX_GPIO5_U;

        case 9:
            return PERIPHS_IO_MUX_SD_DATA2_U;

        case 10:
            return PERIPHS_IO_MUX_SD_DATA3_U;

        case 12:
            return PERIPHS_IO_MUX_MTDI_U;

        case 13:
            return PERIPHS_IO_MUX_MTCK_U;

        case 14:
            return PERIPHS_IO_MUX_MTMS_U;

        case 15:
            return PERIPHS_IO_MUX_MTDO_U;

        default:
            return 0;
    }
}

int gpio_get_func(int gpio_no) {
    switch (gpio_no) {
        case 0:
            return FUNC_GPIO0;

        case 1:
            return FUNC_GPIO1;

        case 2:
            return FUNC_GPIO2;

        case 3:
            return FUNC_GPIO3;

        case 4:
            return FUNC_GPIO4;

        case 5:
            return FUNC_GPIO5;

        case 9:
            return FUNC_GPIO9;

        case 10:
            return FUNC_GPIO10;

        case 12:
            return FUNC_GPIO12;

        case 13:
            return FUNC_GPIO13;

        case 14:
            return FUNC_GPIO14;

        case 15:
            return FUNC_GPIO15;

        default:
            return 0;
    }
}

void gpio_select_func(int gpio_no) {
    /* special treatment for GPIO16 */
    if (gpio_no == 16) {
        WRITE_PERI_REG(PAD_XPD_DCDC_CONF, (READ_PERI_REG(PAD_XPD_DCDC_CONF) & 0xFFFFFFBC) | 0x1);
        WRITE_PERI_REG(RTC_GPIO_CONF, READ_PERI_REG(RTC_GPIO_CONF) & 0xFFFFFFFE);
        return;
    }

    int mux = gpio_get_mux(gpio_no);
    if (!mux) {
        return;
    }

    PIN_FUNC_SELECT(mux, gpio_get_func(gpio_no));
}

void gpio_set_pull(int gpio_no, bool pull) {
    if (gpio_no == 16) {
        uint32 val = READ_PERI_REG(RTC_GPIO_ENABLE);
        if (!pull) {
            val |= 0x8;
        }

        WRITE_PERI_REG(RTC_GPIO_ENABLE, val);
    }
    else {
        int mux = gpio_get_mux(gpio_no);
        if (!mux) {
            return;
        }

        if (pull) {
            PIN_PULLUP_EN(mux);
        }
        else {
            PIN_PULLUP_DIS(mux);
        }
    }
}

void gpio_configure_input(int gpio_no, bool pull) {
    gpio_select_func(gpio_no);

    if (gpio_no == 16) {
        WRITE_PERI_REG(RTC_GPIO_ENABLE, READ_PERI_REG(RTC_GPIO_ENABLE) & 0xFFFFFFFE);
    }
    else {
        GPIO_DIS_OUTPUT(gpio_no);
    }

    gpio_set_pull(gpio_no, pull);

    gpio_configured_state |= 1UL << gpio_no;
    gpio_output_state &= ~(1UL << gpio_no);
    if (pull) {
        gpio_pull_state |= 1UL << gpio_no;
    }
    else {
        gpio_pull_state &= ~(1UL << gpio_no);
    }

    DEBUG_GPIO("configuring GPIO%d as input with pull %d", gpio_no, pull);
}

void gpio_configure_output(int gpio_no, bool initial) {
    gpio_select_func(gpio_no);

    if (gpio_no == 16) {
        WRITE_PERI_REG(RTC_GPIO_ENABLE, (READ_PERI_REG(RTC_GPIO_ENABLE) & 0xFFFFFFFE) | 0x1);

        uint32 val = READ_PERI_REG(RTC_GPIO_OUT);
        WRITE_PERI_REG(RTC_GPIO_OUT, initial ? (val | 0x1) : (val & ~0x1));
    }
    else {
        GPIO_OUTPUT_SET(gpio_no, initial);
    }

    gpio_configured_state |= 1UL << gpio_no;
    gpio_output_state |= 1UL << gpio_no;

    DEBUG_GPIO("configuring GPIO%d as output with initial %d", gpio_no, initial);
}

bool gpio_is_configured(int gpio_no) {
    return !!(gpio_configured_state & (1UL << gpio_no));
}

bool gpio_is_output(int gpio_no) {
    return !!(gpio_output_state & (1UL << gpio_no));
}

bool gpio_get_pull(int gpio_no) {
    return !!(gpio_pull_state & (1UL << gpio_no));
}

void gpio_write_value(int gpio_no, bool value) {
    if (gpio_no == 16) {
        uint32 val = READ_PERI_REG(RTC_GPIO_OUT);
        WRITE_PERI_REG(RTC_GPIO_OUT, value ? (val | 0x1) : (val & ~0x1));
    }
    else {
        GPIO_OUTPUT_SET(gpio_no, !!value);
    }
}

bool gpio_read_value(int gpio_no) {
    if (gpio_no == 16) {
        return !!READ_PERI_REG(RTC_GPIO_IN_DATA);
    }
    else {
        return !!GPIO_INPUT_GET(gpio_no);
    }
}
