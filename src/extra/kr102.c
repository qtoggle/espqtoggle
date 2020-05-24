
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
#include <limits.h>
#include <mem.h>
#include <user_interface.h>
#include <eagle_soc.h>
#include <gpio.h>

#include "espgoodies/common.h"
#include "espgoodies/gpio.h"
#include "espgoodies/system.h"
#include "espgoodies/utils.h"

#include "api.h"
#include "apiutils.h"
#include "common.h"
#include "drivers/uart.h"
#include "ports.h"
#include "extra/kr102.h"


#ifdef HAS_KR102

#define SAMP_INT                        100 /* Milliseconds */
#define HEART_BEAT_INT                  100 /* Milliseconds */

#define DEF_DIGITS                      4
#define MIN_DIGITS                      1
#define MAX_DIGITS                      8

#define DATA_BITS                       8
#define IDLE_VALUE                      -1
#define DIGIT_ESC                       10
#define DIGIT_ENT                       11

#define DEF_INPUT_TIMEOUT               10  /* Seconds */
#define MIN_INPUT_TIMEOUT               1   /* Seconds */
#define MAX_INPUT_TIMEOUT               255 /* Seconds */

#define DEF_CODE_LIFETIME               10  /* Seconds */
#define MIN_CODE_LIFETIME               1   /* Seconds */
#define MAX_CODE_LIFETIME               255 /* Seconds */

#define WD0_GPIO_CONFIG_OFFS            0x00 /* 1 byte */
#define WD1_GPIO_CONFIG_OFFS            0x01 /* 1 byte */
#define DIGITS_CONFIG_OFFS              0x02 /* 1 byte */
#define INPUT_TIMEOUT_CONFIG_OFFS       0x03 /* 1 byte */
#define CODE_LIFETIME_CONFIG_OFFS       0x04 /* 1 byte */


typedef struct {

    uint8                               wd0_gpio;
    uint8                               wd1_gpio;
    uint8                               digits;
    uint8                               input_timeout;
    uint8                               code_lifetime;

    double                              last_value;
    uint64                              current_data;
    uint8                               current_data_bits;
    uint8                               current_digits[MAX_DIGITS];
    uint8                               current_digits_len;
    uint32                              input_start_time; /* Seconds, 0 means no input */
    uint32                              input_end_time;   /* Seconds, 0 means no input */

} extra_info_t;


ICACHE_FLASH_ATTR static void           configure(port_t *port);
ICACHE_FLASH_ATTR static double         read_value(port_t *port);
ICACHE_FLASH_ATTR static void           heart_beat(port_t *port);

static void                             gpio_interrupt_handler(void *arg);
static void                             check_current_data(port_t *port);
static uint8                            data_to_digit(uint64 data);
static int32                            digits_to_value(uint8 *digits, uint8 digits_len);


static attrdef_t wd0_gpio_attrdef = {

    .name = "wd0_gpio",
    .display_name = "WD0 GPIO Number",
    .description = "The GPIO where the WD0 pin is attached.",
    .type = ATTR_TYPE_NUMBER,
    .choices = all_gpio_choices,
    .def = 4,
    .modifiable = TRUE,
    .gs_type = ATTRDEF_GS_TYPE_EXTRA_DATA_1BS,
    .gs_extra_data_offs = WD0_GPIO_CONFIG_OFFS,
    .extra_info_cache_offs = ATTRDEF_CACHE_EXTRA_INFO_FIELD(extra_info_t, wd0_gpio)

};

static attrdef_t wd1_gpio_attrdef = {

    .name = "wd1_gpio",
    .display_name = "WD1 GPIO Number",
    .description = "The GPIO where the WD1 pin is attached.",
    .type = ATTR_TYPE_NUMBER,
    .choices = all_gpio_choices,
    .def = 5,
    .modifiable = TRUE,
    .gs_type = ATTRDEF_GS_TYPE_EXTRA_DATA_1BS,
    .gs_extra_data_offs = WD1_GPIO_CONFIG_OFFS,
    .extra_info_cache_offs = ATTRDEF_CACHE_EXTRA_INFO_FIELD(extra_info_t, wd1_gpio)
};

static attrdef_t digits_attrdef = {

    .name = "digits",
    .display_name = "Digits",
    .description = "Number of digits required for input code.",
    .type = ATTR_TYPE_NUMBER,
    .modifiable = TRUE,
    .min = MIN_DIGITS,
    .max = MAX_DIGITS,
    .def = DEF_DIGITS,
    .integer = TRUE,
    .gs_type = ATTRDEF_GS_TYPE_EXTRA_DATA_1BU,
    .gs_extra_data_offs = DIGITS_CONFIG_OFFS,
    .extra_info_cache_offs = ATTRDEF_CACHE_EXTRA_INFO_FIELD(extra_info_t, digits)

};

static attrdef_t input_timeout_attrdef = {

    .name = "input_timeout",
    .display_name = "Input Timeout",
    .description = "The time the user has to input a code.",
    .type = ATTR_TYPE_NUMBER,
    .unit = "s",
    .modifiable = TRUE,
    .min = MIN_INPUT_TIMEOUT,
    .max = MAX_INPUT_TIMEOUT,
    .def = DEF_INPUT_TIMEOUT,
    .integer = TRUE,
    .gs_type = ATTRDEF_GS_TYPE_EXTRA_DATA_1BU,
    .gs_extra_data_offs = INPUT_TIMEOUT_CONFIG_OFFS,
    .extra_info_cache_offs = ATTRDEF_CACHE_EXTRA_INFO_FIELD(extra_info_t, input_timeout)

};

static attrdef_t code_lifetime_attrdef = {

    .name = "code_lifetime",
    .display_name = "Code Lifetime",
    .description = "Indicates how long a code remains entered.",
    .type = ATTR_TYPE_NUMBER,
    .unit = "s",
    .modifiable = TRUE,
    .min = MIN_CODE_LIFETIME,
    .max = MAX_CODE_LIFETIME,
    .def = DEF_CODE_LIFETIME,
    .integer = TRUE,
    .gs_type = ATTRDEF_GS_TYPE_EXTRA_DATA_1BU,
    .gs_extra_data_offs = CODE_LIFETIME_CONFIG_OFFS,
    .extra_info_cache_offs = ATTRDEF_CACHE_EXTRA_INFO_FIELD(extra_info_t, code_lifetime)

};

static attrdef_t *attrdefs[] = {

    &wd0_gpio_attrdef,
    &wd1_gpio_attrdef,
    &digits_attrdef,
    &input_timeout_attrdef,
    &code_lifetime_attrdef,
    NULL

};


static extra_info_t kr0_extra_info;


#ifdef HAS_KR0

static port_t _kr0 = {

    .slot = PORT_SLOT_AUTO,

    .id = KR0_ID,
    .type = PORT_TYPE_NUMBER,
    .min = UNDEFINED,
    .max = UNDEFINED,
    .step = UNDEFINED,

    .attrdefs = attrdefs,
    .extra_info = &kr0_extra_info,
    .def_sampling_interval = SAMP_INT,

    .read_value = read_value,
    .configure = configure,
    .heart_beat = heart_beat,
    .heart_beat_interval = HEART_BEAT_INT

};

port_t *kr0 = &_kr0;

#endif


void configure(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    DEBUG_KR102("using wd0_gpio = %d", extra_info->wd0_gpio);
    DEBUG_KR102("using wd1_gpio = %d", extra_info->wd1_gpio);
    DEBUG_KR102("using digits = %d", extra_info->digits);
    DEBUG_KR102("using input_timeout = %d s", extra_info->input_timeout);
    DEBUG_KR102("using code_lifetime = %d s", extra_info->code_lifetime);

    ETS_GPIO_INTR_DISABLE();
    ETS_GPIO_INTR_ATTACH(gpio_interrupt_handler, port);

    /* Enable interrupt for WD0 GPIO */
    uint8 wd0_gpio = extra_info->wd0_gpio;
    gpio_configure_input(wd0_gpio, /* pull = */ TRUE);
    gpio_register_set(GPIO_PIN_ADDR(wd0_gpio),
                      GPIO_PIN_INT_TYPE_SET(GPIO_PIN_INTR_DISABLE) |
                      GPIO_PIN_PAD_DRIVER_SET(GPIO_PAD_DRIVER_DISABLE) |
                      GPIO_PIN_SOURCE_SET(GPIO_AS_PIN_SOURCE));
    GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, BIT(wd0_gpio));
    gpio_pin_intr_state_set(GPIO_ID_PIN(wd0_gpio), GPIO_PIN_INTR_NEGEDGE);

    /* Enable interrupt for WD1 GPIO */
    uint8 wd1_gpio = extra_info->wd1_gpio;
    gpio_configure_input(wd1_gpio, /* pull = */ TRUE);
    gpio_register_set(GPIO_PIN_ADDR(wd1_gpio),
                      GPIO_PIN_INT_TYPE_SET(GPIO_PIN_INTR_DISABLE) |
                      GPIO_PIN_PAD_DRIVER_SET(GPIO_PAD_DRIVER_DISABLE) |
                      GPIO_PIN_SOURCE_SET(GPIO_AS_PIN_SOURCE));
    GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, BIT(wd1_gpio));
    gpio_pin_intr_state_set(GPIO_ID_PIN(wd1_gpio), GPIO_PIN_INTR_NEGEDGE);

    extra_info->last_value = IDLE_VALUE;
    extra_info->current_data = 0;
    extra_info->current_data_bits = 0;
    extra_info->current_digits_len = 0;
    extra_info->input_start_time = 0;
    extra_info->input_end_time = 0;

    if (!extra_info->digits) {
        extra_info->digits = DEF_DIGITS;
    }
    if (!extra_info->input_timeout) {
        extra_info->input_timeout = DEF_INPUT_TIMEOUT;
    }
    if (!extra_info->code_lifetime) {
        extra_info->code_lifetime = DEF_CODE_LIFETIME;
    }

    ETS_GPIO_INTR_ENABLE();
}

double read_value(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    return extra_info->last_value;
}

void heart_beat(port_t *port) {
    extra_info_t *extra_info = port->extra_info;
    uint32 now = system_uptime();

    check_current_data(port);

    if ((extra_info->input_start_time > 0) &&
        (now - extra_info->input_start_time > extra_info->input_timeout)) {
        DEBUG_KR102("input timeout");

        extra_info->input_start_time = 0;
        extra_info->input_end_time = 0;
        extra_info->current_data = 0;
        extra_info->current_data_bits = 0;
        extra_info->current_digits_len = 0;
        extra_info->last_value = IDLE_VALUE;
    }

    if ((extra_info->input_end_time > 0) &&
        (now - extra_info->input_end_time > extra_info->code_lifetime)) {
        DEBUG_KR102("code lifetime ended");

        extra_info->input_end_time = 0;
        extra_info->last_value = IDLE_VALUE;
    }
}


void gpio_interrupt_handler(void *arg) {
    port_t *port = arg;
    extra_info_t *extra_info = port->extra_info;

    uint8 wd0_gpio = extra_info->wd0_gpio;
    uint8 wd1_gpio = extra_info->wd1_gpio;
    uint64 current_data = extra_info->current_data;
    uint32 gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);

    bool our_interrupt = FALSE;
    if (gpio_status & BIT(wd0_gpio) && !gpio_read_value(wd0_gpio)) {
        current_data <<= 1;
        our_interrupt = TRUE;
    }
    else if (gpio_status & BIT(wd1_gpio) && !gpio_read_value(wd1_gpio)) {
        current_data <<= 1;
        current_data++;
        our_interrupt = TRUE;
    }

    if (our_interrupt) {
        if (!extra_info->input_start_time) {
            extra_info->input_start_time = system_uptime();
        }

        extra_info->current_data_bits++;
        extra_info->current_data = current_data;
    }

    /* Clear interrupt status */
    GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status);
}

void check_current_data(port_t *port) {
    extra_info_t *extra_info = port->extra_info;

    if (extra_info->current_data_bits >= DATA_BITS) {
        uint8 digit = data_to_digit(extra_info->current_data);
        DEBUG_KR102("got digit %d", digit);

        if (digit == DIGIT_ESC) {
            DEBUG_KR102("erasing current state");
            extra_info->input_start_time = 0;
            extra_info->input_end_time = 0;
            extra_info->current_digits_len = 0;
            extra_info->last_value = IDLE_VALUE;
        }
        else if (digit == DIGIT_ENT) {
            extra_info->last_value = digits_to_value(extra_info->current_digits, extra_info->current_digits_len);
            DEBUG_KR102("using entered value: %08d", (uint32) extra_info->last_value);
            extra_info->input_start_time = 0;
            extra_info->input_end_time = system_uptime();
            extra_info->current_digits_len = 0;
        }
        else {
            extra_info->current_digits[extra_info->current_digits_len++] = digit;
        }

        extra_info->current_data_bits = 0;
        extra_info->current_data = 0;
    }

    if (extra_info->current_digits_len >= extra_info->digits) {
        extra_info->last_value = digits_to_value(extra_info->current_digits, extra_info->digits);
        DEBUG_KR102("got all digits: %08d", (uint32) extra_info->last_value);
        extra_info->input_start_time = 0;
        extra_info->input_end_time = system_uptime();
        extra_info->current_digits_len = 0;
    }
}

uint8 data_to_digit(uint64 data) {
    return (data & 0xFF) / 15 - 1; /* Keys are multiple of 15, starting at 1x15 */
}

int32 digits_to_value(uint8 *digits, uint8 digits_len) {
    if (digits_len == 0) {
        return IDLE_VALUE;
    }

    uint32 i, value = 0;
    for (i = 0; i < digits_len; i++) {
        value = value * 10 + digits[i];
    }

    return value;
}


void kr102_init_ports(void) {
#ifdef HAS_KR0
    port_register(kr0);
#endif
}


#endif  /* HAS_KR102 */
