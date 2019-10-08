
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
#include <gpio.h>
#include <user_interface.h>

#include "espgoodies/common.h"
#include "espgoodies/utils.h"
#include "espgoodies/system.h"

#include "ports.h"
#include "extra/pwdetect.h"


#ifdef HAS_PWDETECT


#define STATE_IDLE                      0
#define STATE_TRIGGER_STARTED           1
#define STATE_TRIGGER_ENDED             2
#define STATE_PULSE_STARTED             3

#define MEASURE_INTERVAL_CONFIG_OFFS    0x00    /* 2 bytes */
#define TRIGGER_INTERVAL_CONFIG_OFFS    0x02    /* 2 bytes */
#define PULSE_START_TO_CONFIG_OFFS      0x04    /* 2 bytes */
#define PULSE_STOP_TO_CONFIG_OFFS       0x06    /* 2 bytes */
#define TRIGGER_GPIO_CONFIG_OFFS        0x08    /* 1 byte */
#define READ_GPIO_CONFIG_OFFS           0x09    /* 1 byte */

#define FLAG_TRIGGER_INVERTED           0x01000000
#define FLAG_PULSE_INVERTED             0x02000000

#define GPIO_CHOICES_LEN                8

    /* all times below are in microseconds */
#define MEASURE_INTERVAL_DEF            1000
#define MEASURE_INTERVAL_MIN            100
#define MEASURE_INTERVAL_MAX            6000000
#define MEASURE_INTERVAL_STEP           100

#define TRIGGER_INTERVAL_DEF            100
#define TRIGGER_INTERVAL_MIN            100
#define TRIGGER_INTERVAL_MAX            6000000
#define TRIGGER_INTERVAL_STEP           100

#define PULSE_TIMEOUT_DEF               500000
#define PULSE_TIMEOUT_MIN               100
#define PULSE_TIMEOUT_MAX               6000000
#define PULSE_TIMEOUT_STEP              100


typedef struct {

    uint8                               state;
    int64                               last_transition_time;
    double                              measured_width;

    uint8                               trigger_gpio;
    uint8                               read_gpio;

    uint32                              measure_interval;
    uint32                              trigger_interval;
    uint32                              pulse_start_timeout;
    uint32                              pulse_stop_timeout;

} extra_info_t;


#define get_state(p)                    (((extra_info_t *) (p)->extra_info)->state)
#define set_state(p, v)                 {((extra_info_t *) (p)->extra_info)->state = v;}

#define get_last_transition_time(p)     (((extra_info_t *) (p)->extra_info)->last_transition_time)
#define set_last_transition_time(p, v)  {((extra_info_t *) (p)->extra_info)->last_transition_time = v;}

#define get_measured_width(p)           (((extra_info_t *) (p)->extra_info)->measured_width)
#define set_measured_width(p, v)        {((extra_info_t *) (p)->extra_info)->measured_width = v;}

#define get_trigger_gpio(p)             (((extra_info_t *) (p)->extra_info)->trigger_gpio)
#define set_trigger_gpio(p, v)          {((extra_info_t *) (p)->extra_info)->trigger_gpio = v;}

#define get_read_gpio(p)                (((extra_info_t *) (p)->extra_info)->read_gpio)
#define set_read_gpio(p, v)             {((extra_info_t *) (p)->extra_info)->read_gpio = v;}

#define get_measure_interval(p)         (((extra_info_t *) (p)->extra_info)->measure_interval)
#define set_measure_interval(p, v)      {((extra_info_t *) (p)->extra_info)->measure_interval = v;}

#define get_trigger_interval(p)         (((extra_info_t *) (p)->extra_info)->trigger_interval)
#define set_trigger_interval(p, v)      {((extra_info_t *) (p)->extra_info)->trigger_interval = v;}

#define get_trigger_inverted(p)         (!!((p)->flags & FLAG_TRIGGER_INVERTED))
#define set_trigger_inverted(p, v)      {if (v) (p)->flags |= FLAG_TRIGGER_INVERTED; \
                                         else (p)->flags &= ~FLAG_TRIGGER_INVERTED;}

#define get_pulse_start_timeout(p)      (((extra_info_t *) (p)->extra_info)->pulse_start_timeout)
#define set_pulse_start_timeout(p, v)   {((extra_info_t *) (p)->extra_info)->pulse_start_timeout = v;}

#define get_pulse_stop_timeout(p)       (((extra_info_t *) (p)->extra_info)->pulse_stop_timeout)
#define set_pulse_stop_timeout(p, v)    {((extra_info_t *) (p)->extra_info)->pulse_stop_timeout = v;}

#define get_pulse_inverted(p)           (!!((p)->flags & FLAG_PULSE_INVERTED))
#define set_pulse_inverted(p, v)        {if (v) (p)->flags |= FLAG_PULSE_INVERTED; \
                                         else (p)->flags &= ~FLAG_PULSE_INVERTED;}


ICACHE_FLASH_ATTR static double         read_value(port_t *port);
ICACHE_FLASH_ATTR static void           configure(port_t *port);
ICACHE_FLASH_ATTR static void           heart_beat(port_t *port);

ICACHE_FLASH_ATTR static int            attr_get_trigger_gpio(port_t *port);
ICACHE_FLASH_ATTR static void           attr_set_trigger_gpio(port_t *port, int index);

ICACHE_FLASH_ATTR static int            attr_get_read_gpio(port_t *port);
ICACHE_FLASH_ATTR static void           attr_set_read_gpio(port_t *port, int index);

ICACHE_FLASH_ATTR static void           attr_set_measure_interval(port_t *port, int value);
ICACHE_FLASH_ATTR static int            attr_get_measure_interval(port_t *port);

ICACHE_FLASH_ATTR static void           attr_set_trigger_interval(port_t *port, int value);
ICACHE_FLASH_ATTR static int            attr_get_trigger_interval(port_t *port);

ICACHE_FLASH_ATTR static void           attr_set_trigger_inverted(port_t *port, bool value);
ICACHE_FLASH_ATTR static bool           attr_get_trigger_inverted(port_t *port);

ICACHE_FLASH_ATTR static void           attr_set_pulse_start_timeout(port_t *port, int value);
ICACHE_FLASH_ATTR static int            attr_get_pulse_start_timeout(port_t *port);

ICACHE_FLASH_ATTR static void           attr_set_pulse_stop_timeout(port_t *port, int value);
ICACHE_FLASH_ATTR static int            attr_get_pulse_stop_timeout(port_t *port);

ICACHE_FLASH_ATTR static void           attr_set_pulse_inverted(port_t *port, bool value);
ICACHE_FLASH_ATTR static bool           attr_get_pulse_inverted(port_t *port);

ICACHE_FLASH_ATTR static void           trigger_start(port_t *port);
ICACHE_FLASH_ATTR static void           trigger_stop(port_t *port);
ICACHE_FLASH_ATTR static bool           level_read(port_t *port);
ICACHE_FLASH_ATTR static bool           level_read_single(port_t *port);


static uint8                            gpio_mapping[] = {0, 2, 4, 5, 12, 13, 14, 15};
static char                           * gpio_choices[] = {"gpio 0", "gpio 2", "gpio 4", "gpio 5", "gpio 12", "gpio 13",
                                                          "gpio 14", "gpio 15", NULL};

static attrdef_t trigger_gpio_attrdef = {

    .name = "trigger_gpio",
    .display_name = "Trigger GPIO",
    .description = "The trigger GPIO.",
    .type = ATTR_TYPE_STRING,
    .choices = gpio_choices,
    .modifiable = TRUE,
    .set = attr_set_trigger_gpio,
    .get = attr_get_trigger_gpio

};

static attrdef_t read_gpio_attrdef = {

    .name = "read_gpio",
    .display_name = "Read GPIO",
    .description = "The read GPIO.",
    .type = ATTR_TYPE_STRING,
    .choices = gpio_choices,
    .modifiable = TRUE,
    .set = attr_set_read_gpio,
    .get = attr_get_read_gpio

};

static attrdef_t measure_interval_attrdef = {

    .name = "measure_interval",
    .display_name = "Measure Interval",
    .description = "The interval between two successive pulse-width measurements.",
    .unit = "microseconds",
    .type = ATTR_TYPE_NUMBER,
    .modifiable = TRUE,
    .min = MEASURE_INTERVAL_MIN,
    .max = MEASURE_INTERVAL_MAX,
    .step = MEASURE_INTERVAL_STEP,
    .integer = TRUE,
    .set = attr_set_measure_interval,
    .get = attr_get_measure_interval

};

static attrdef_t trigger_interval_attrdef = {

    .name = "trigger_interval",
    .display_name = "Trigger Interval",
    .description = "The trigger signal interval.",
    .unit = "microseconds",
    .type = ATTR_TYPE_NUMBER,
    .modifiable = TRUE,
    .min = TRIGGER_INTERVAL_MIN,
    .max = TRIGGER_INTERVAL_MAX,
    .step = TRIGGER_INTERVAL_STEP,
    .integer = TRUE,
    .set = attr_set_trigger_interval,
    .get = attr_get_trigger_interval

};

static attrdef_t trigger_inverted_attrdef = {

    .name = "trigger_inverted",
    .display_name = "Trigger Inverted",
    .description = "Indicates whether the trigger signal is normal or inverted.",
    .type = ATTR_TYPE_BOOLEAN,
    .modifiable = TRUE,
    .integer = TRUE,
    .set = attr_set_trigger_inverted,
    .get = attr_get_trigger_inverted

};

static attrdef_t pulse_start_timeout_attrdef = {

    .name = "pulse_start_timeout",
    .display_name = "Pulse Start Timeout",
    .description = "The timeout to wait for the pulse start.",
    .unit = "microseconds",
    .type = ATTR_TYPE_NUMBER,
    .modifiable = TRUE,
    .min = PULSE_TIMEOUT_MIN,
    .max = PULSE_TIMEOUT_MAX,
    .step = PULSE_TIMEOUT_STEP,
    .integer = TRUE,
    .set = attr_set_pulse_start_timeout,
    .get = attr_get_pulse_start_timeout

};

static attrdef_t pulse_stop_timeout_attrdef = {

    .name = "pulse_stop_timeout",
    .display_name = "Pulse Stop Timeout",
    .description = "The timeout to wait for the pulse stop.",
    .unit = "microseconds",
    .type = ATTR_TYPE_NUMBER,
    .modifiable = TRUE,
    .min = PULSE_TIMEOUT_MIN,
    .max = PULSE_TIMEOUT_MAX,
    .step = PULSE_TIMEOUT_STEP,
    .integer = TRUE,
    .set = attr_set_pulse_stop_timeout,
    .get = attr_get_pulse_stop_timeout

};

static attrdef_t pulse_inverted_attrdef = {

    .name = "pulse_inverted",
    .display_name = "Pulse Inverted",
    .description = "Indicates whether the pulse signal is normal or inverted.",
    .type = ATTR_TYPE_BOOLEAN,
    .modifiable = TRUE,
    .set = attr_set_pulse_inverted,
    .get = attr_get_pulse_inverted

};

static attrdef_t *attrdefs[] = {

    &trigger_gpio_attrdef,
    &read_gpio_attrdef,
    &measure_interval_attrdef,
    &trigger_interval_attrdef,
    &trigger_inverted_attrdef,
    &pulse_start_timeout_attrdef,
    &pulse_stop_timeout_attrdef,
    &pulse_inverted_attrdef,
    NULL

};

#ifdef HAS_PWDETECT0
static extra_info_t pwdetect0_extra_info = {

    .state = STATE_IDLE,
    .last_transition_time = -1,
    .measured_width = UNDEFINED

};

static port_t _pwdetect0 = {

    .slot = PORT_SLOT_EXTRA0,

    .id = PWDETECT0_ID,
    .type = PORT_TYPE_NUMBER,
    .min = UNDEFINED,
    .max = UNDEFINED,
    .step = UNDEFINED,
    .integer = TRUE,

    .extra_info = &pwdetect0_extra_info,

    .read_value = read_value,
    .configure = configure,
    .heart_beat = heart_beat,

    .attrdefs = attrdefs

};

port_t *pwdetect0 = &_pwdetect0;
#endif

#ifdef HAS_PWDETECT1
static extra_info_t pwdetect1_extra_info = {

    .state = STATE_IDLE,
    .last_transition_time = -1,
    .measured_width = UNDEFINED

};

static port_t _pwdetect1 = {

    .slot = PORT_SLOT_EXTRA1,

    .id = PWDETECT1_ID,
    .type = PORT_TYPE_NUMBER,
    .min = UNDEFINED,
    .max = UNDEFINED,
    .step = UNDEFINED,
    .integer = TRUE,

    .extra_info = &pwdetect1_extra_info,

    .read_value = read_value,
    .configure = configure,
    .heart_beat = heart_beat,

    .attrdefs = attrdefs

};

port_t *pwdetect1 = &_pwdetect1;
#endif

#ifdef HAS_PWDETECT2
static extra_info_t pwdetect2_extra_info = {

    .state = STATE_IDLE,
    .last_transition_time = -1,
    .measured_width = UNDEFINED

};

static port_t _pwdetect2 = {

    .slot = PORT_SLOT_EXTRA2,

    .id = PWDETECT2_ID,
    .type = PORT_TYPE_NUMBER,
    .min = UNDEFINED,
    .max = UNDEFINED,
    .step = UNDEFINED,
    .integer = TRUE,

    .extra_info = &pwdetect2_extra_info,

    .read_value = read_value,
    .configure = configure,
    .heart_beat = heart_beat,

    .attrdefs = attrdefs

};

port_t *pwdetect2 = &_pwdetect2;
#endif

#ifdef HAS_PWDETECT3
static extra_info_t pwdetect3_extra_info = {

    .state = STATE_IDLE,
    .last_transition_time = -1,
    .measured_width = UNDEFINED

};

static port_t _pwdetect3 = {

    .slot = PORT_SLOT_EXTRA3,

    .id = PWDETECT3_ID,
    .type = PORT_TYPE_NUMBER,
    .min = UNDEFINED,
    .max = UNDEFINED,
    .step = UNDEFINED,
    .integer = TRUE,

    .extra_info = &pwdetect3_extra_info,

    .read_value = read_value,
    .configure = configure,
    .heart_beat = heart_beat,

    .attrdefs = attrdefs

};

port_t *pwdetect3 = &_pwdetect3;
#endif

#ifdef HAS_PWDETECT4
static extra_info_t pwdetect4_extra_info = {

    .state = STATE_IDLE,
    .last_transition_time = -1,
    .measured_width = UNDEFINED

};

static port_t _pwdetect4 = {

    .slot = PORT_SLOT_EXTRA4,

    .id = PWDETECT4_ID,
    .type = PORT_TYPE_NUMBER,
    .min = UNDEFINED,
    .max = UNDEFINED,
    .step = UNDEFINED,
    .integer = TRUE,

    .extra_info = &pwdetect4_extra_info,

    .read_value = read_value,
    .configure = configure,
    .heart_beat = heart_beat,

    .attrdefs = attrdefs

};

port_t *pwdetect4 = &_pwdetect4;
#endif

#ifdef HAS_PWDETECT5
static extra_info_t pwdetect5_extra_info = {

    .state = STATE_IDLE,
    .last_transition_time = -1,
    .measured_width = UNDEFINED

};

static port_t _pwdetect5 = {

    .slot = PORT_SLOT_EXTRA5,

    .id = PWDETECT5_ID,
    .type = PORT_TYPE_NUMBER,
    .min = UNDEFINED,
    .max = UNDEFINED,
    .step = UNDEFINED,
    .integer = TRUE,

    .extra_info = &pwdetect5_extra_info,

    .read_value = read_value,
    .configure = configure,
    .heart_beat = heart_beat,

    .attrdefs = attrdefs

};

port_t *pwdetect5 = &_pwdetect5;
#endif


double read_value(port_t *port) {
    return get_measured_width(port);
}

void configure(port_t *port) {
    gpio_select_func(get_trigger_gpio(port));
    gpio_select_func(get_read_gpio(port));

    GPIO_DIS_OUTPUT(get_read_gpio(port));

    DEBUG_PWDETECT(port, "using measure interval = %d us", get_measure_interval(port));
    DEBUG_PWDETECT(port, "using trigger interval = %d us", get_trigger_interval(port));
    if (get_trigger_inverted(port)) {
        DEBUG_PWDETECT(port, "trigger signal is inverted");
    }
    DEBUG_PWDETECT(port, "using pulse start timeout = %d us", get_pulse_start_timeout(port));
    DEBUG_PWDETECT(port, "using pulse stop timeout = %d us", get_pulse_stop_timeout(port));
    if (get_pulse_inverted(port)) {
        DEBUG_PWDETECT(port, "pulse signal is inverted");
    }
}

void heart_beat(port_t *port) {
    int64 now = system_uptime_us();
    int64 last_transition_time = get_last_transition_time(port);

    if (last_transition_time == -1) {
        set_last_transition_time(port, now);
        return;
    }

    int64 delta = now - last_transition_time;
    uint8 state = get_state(port);
    bool level;

    switch (state) {
        case STATE_IDLE:
            if (delta < get_measure_interval(port)) {
                break;
            }

            trigger_start(port);
            state = STATE_TRIGGER_STARTED;
            last_transition_time = now;

            break;

        case STATE_TRIGGER_STARTED:
            if (delta < get_trigger_interval(port)) {
                break;
            }

            trigger_stop(port);
            state = STATE_TRIGGER_ENDED;
            last_transition_time = now;

            level = level_read(port);
            if (level) {
                DEBUG_PWDETECT(port, "unexpected read level, canceling measurement");
                state = STATE_IDLE;
                last_transition_time = now;
            }

            break;

        case STATE_TRIGGER_ENDED:
            level = level_read(port);
            if (level) {
                state = STATE_PULSE_STARTED;
                last_transition_time = now;
            }
            else if (delta > get_pulse_start_timeout(port)) {
                DEBUG_PWDETECT(port, "timeout waiting for pulse start, canceling measurement");
                state = STATE_IDLE;
                last_transition_time = now;
            }

            break;

        case STATE_PULSE_STARTED:
            level = level_read(port);
            if (!level) {
                state = STATE_IDLE;
                last_transition_time = now;
                set_measured_width(port, delta);
            }
            else if (delta > get_pulse_stop_timeout(port)) {
                DEBUG_PWDETECT(port, "timeout waiting for pulse end, canceling measurement");
                state = STATE_IDLE;
                last_transition_time = now;
            }

            break;
    }

    set_last_transition_time(port, last_transition_time);
    set_state(port, state);
}

int attr_get_trigger_gpio(port_t *port) {
    int i;
    uint8 value;

    /* read from persisted data */
    memcpy(&value, port->extra_data + TRIGGER_GPIO_CONFIG_OFFS, 1);

    /* update cached value */
    set_trigger_gpio(port, value);

    for (i = 0; i < GPIO_CHOICES_LEN; i++) {
        if (gpio_mapping[i] == value) {
            /* return choice index */
            return i;
        }
    }

    return 0;
}

void attr_set_trigger_gpio(port_t *port, int index) {
    uint8 value = gpio_mapping[index];

    /* update cached value */
    set_trigger_gpio(port, value);

    /* write to persisted data */
    memcpy(port->extra_data + TRIGGER_GPIO_CONFIG_OFFS, &value, 1);
}

int attr_get_read_gpio(port_t *port) {
    int i;
    uint8 value;

    /* read from persisted data */
    memcpy(&value, port->extra_data + READ_GPIO_CONFIG_OFFS, 1);

    /* update cached value */
    set_read_gpio(port, value);

    for (i = 0; i < GPIO_CHOICES_LEN; i++) {
        if (gpio_mapping[i] == value) {
            /* return choice index */
            return i;
        }
    }

    return 0;
}

void attr_set_read_gpio(port_t *port, int index) {
    uint8 value = gpio_mapping[index];

    /* update cached value */
    set_read_gpio(port, value);

    /* write to persisted data */
    memcpy(port->extra_data + READ_GPIO_CONFIG_OFFS, &value, 1);
}

void attr_set_measure_interval(port_t *port, int value) {
    uint16 config_value = value / 100;

    /* update cached value */
    set_measure_interval(port, value);

    /* write to persisted data */
    memcpy(port->extra_data + MEASURE_INTERVAL_CONFIG_OFFS, &config_value, 2);
}

int attr_get_measure_interval(port_t *port) {
    uint32 config_value;
    uint16 value;

    /* read from persisted data */
    memcpy(&config_value, port->extra_data + MEASURE_INTERVAL_CONFIG_OFFS, 2);
    value = config_value * 100;

    /* apply default value */
    if (!value) {
        value = MEASURE_INTERVAL_DEF;
    }

    /* update cached value */
    set_measure_interval(port, value);

    return value;
}

void attr_set_trigger_interval(port_t *port, int value) {
    uint16 config_value = value / 100;

    /* update cached value */
    set_trigger_interval(port, value);

    /* write to persisted data */
    memcpy(port->extra_data + TRIGGER_INTERVAL_CONFIG_OFFS, &config_value, 2);
}

int attr_get_trigger_interval(port_t *port) {
    uint16 config_value;
    uint32 value;

    /* read from persisted data */
    memcpy(&config_value, port->extra_data + TRIGGER_INTERVAL_CONFIG_OFFS, 2);
    value = config_value * 100;

    /* apply default value */
    if (!value) {
        value = TRIGGER_INTERVAL_DEF;
    }

    /* update cached value */
    set_trigger_interval(port, value);

    return value;
}

void attr_set_trigger_inverted(port_t *port, bool value) {
    set_trigger_inverted(port, value);
}

bool attr_get_trigger_inverted(port_t *port) {
    return get_trigger_inverted(port);
}

void attr_set_pulse_start_timeout(port_t *port, int value) {
    uint16 config_value = value / 100;

    /* update cached value */
    set_pulse_start_timeout(port, value);

    /* write to persisted data */
    memcpy(port->extra_data + PULSE_START_TO_CONFIG_OFFS, &config_value, 2);
}

int attr_get_pulse_start_timeout(port_t *port) {
    uint16 config_value;
    uint32 value;

    /* read from persisted data */
    memcpy(&config_value, port->extra_data + PULSE_START_TO_CONFIG_OFFS, 2);
    value = config_value * 100;

    /* apply default value */
    if (!value) {
        value = PULSE_TIMEOUT_DEF;
    }

    /* update cached value */
    set_pulse_start_timeout(port, value);

    return value;
}

void attr_set_pulse_stop_timeout(port_t *port, int value) {
    uint16 config_value = value / 100;

    /* update cached value */
    set_pulse_stop_timeout(port, value);

    /* write to persisted data */
    memcpy(port->extra_data + PULSE_STOP_TO_CONFIG_OFFS, &config_value, 2);
}

int attr_get_pulse_stop_timeout(port_t *port) {
    uint16 config_value;
    uint32 value;

    /* read from persisted data */
    memcpy(&config_value, port->extra_data + PULSE_STOP_TO_CONFIG_OFFS, 2);
    value = config_value * 100;

    /* apply default value */
    if (!value) {
        value = PULSE_TIMEOUT_DEF;
    }

    /* update cached value */
    set_pulse_stop_timeout(port, value);

    return value;
}

void attr_set_pulse_inverted(port_t *port, bool value) {
    set_pulse_inverted(port, value);
}

bool attr_get_pulse_inverted(port_t *port) {
    return get_pulse_inverted(port);
}


void trigger_start(port_t *port) {
    GPIO_OUTPUT_SET(get_trigger_gpio(port), !get_trigger_inverted(port));
}

void trigger_stop(port_t *port) {
    GPIO_OUTPUT_SET(get_trigger_gpio(port), get_trigger_inverted(port));
}

bool level_read_single(port_t *port) {
    bool level = !GPIO_INPUT_GET(get_read_gpio(port));
    if (!get_pulse_inverted(port)) {
        level = !level;
    }

    return level;
}

bool level_read(port_t *port) {
    bool level1 = level_read_single(port);
    os_delay_us(10);
    bool level2 = level_read_single(port);
    os_delay_us(10);
    bool level3 = level_read_single(port);

    return (level1 && level2) || (level1 && level3) || (level2 && level3);
}


void pwdetect_init_ports(void) {
#ifdef HAS_PWDETECT0
    port_register(pwdetect0);
#endif

#ifdef HAS_PWDETECT1
    port_register(pwdetect1);
#endif

#ifdef HAS_PWDETECT2
    port_register(pwdetect2);
#endif

#ifdef HAS_PWDETECT3
    port_register(pwdetect3);
#endif

#ifdef HAS_PWDETECT4
    port_register(pwdetect4);
#endif

#ifdef HAS_PWDETECT5
    port_register(pwdetect5);
#endif
}


#endif  /* HAS_PWDETECT */
