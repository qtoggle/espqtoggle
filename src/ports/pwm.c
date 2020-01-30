
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
#include <c_types.h>
#include <mem.h>
#include <pwm.h>

#include "espgoodies/common.h"
#include "espgoodies/gpio.h"

#include "common.h"
#include "ports/pwm.h"


#ifdef HAS_PWM


#define PWM_REGISTER(port) {                \
    (port)->mapped = pwm_count + 1;         \
    (port)->flags |= PORT_FLAG_OUTPUT;      \
    port_register(port);                    \
}

#define PWM_SET_INFO(info, func, pin) {     \
    (info)[0] = gpio_get_mux(pin);          \
    (info)[1] = (func);                     \
    (info)[2] = (pin);                      \
}

#define PWM_FREQ_SLOT               PORT_SLOT_AUTO
#define PWM_FREQ_MIN                1       /* Hz */
#define PWM_FREQ_MAX                50000   /* Hz */
#define PWM_FREQ_DEF                1000    /* Hz */

#define PWM_MIN                     0
#define PWM_MAX                     100


ICACHE_FLASH_ATTR static double     freq_read_value(port_t *port);
ICACHE_FLASH_ATTR static bool       freq_write_value(port_t *port, double value);
ICACHE_FLASH_ATTR static void       freq_configure(port_t *port);

ICACHE_FLASH_ATTR static double     read_value(port_t *port);
ICACHE_FLASH_ATTR static bool       write_value(port_t *port, double value);
ICACHE_FLASH_ATTR static void       configure(port_t *port);


static int                          pwm_cur_freq = PWM_FREQ_DEF;
static char                       * pwm_cur_duty = NULL;
static int                          pwm_count = 0;

static port_t _pwm_freq = {

    .slot = PWM_FREQ_SLOT,

    .id = "pwm_freq",
    .type = PORT_TYPE_NUMBER,
    .flags = PORT_FLAG_OUTPUT,

    .value = PWM_FREQ_DEF,
    .min = PWM_FREQ_MIN,
    .max = PWM_FREQ_MAX,
    .step = UNDEFINED,
    .integer = TRUE,

    .read_value = freq_read_value,
    .write_value = freq_write_value,
    .configure = freq_configure

};

port_t *pwm_freq = &_pwm_freq;

#ifdef HAS_PWM0
static port_t _pwm0 = {

    .slot = 0,

    .id = PWM0_ID,
    .type = PORT_TYPE_NUMBER,
    .flags = PORT_FLAG_OUTPUT,

    .min = PWM_MIN,
    .max = PWM_MAX,
    .step = UNDEFINED,
    .integer = TRUE,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure

};

port_t *pwm0 = &_pwm0;
#endif

#ifdef HAS_PWM1
static port_t _pwm1 = {

    .slot = 1,

    .id = PWM1_ID,
    .type = PORT_TYPE_NUMBER,
    .flags = PORT_FLAG_OUTPUT,

    .min = PWM_MIN,
    .max = PWM_MAX,
    .step = UNDEFINED,
    .integer = TRUE,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure

};

port_t *pwm1 = &_pwm1;
#endif

#ifdef HAS_PWM2
static port_t _pwm2 = {

    .slot = 2,

    .id = PWM2_ID,
    .type = PORT_TYPE_NUMBER,
    .flags = PORT_FLAG_OUTPUT,

    .min = PWM_MIN,
    .max = PWM_MAX,
    .step = UNDEFINED,
    .integer = TRUE,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure

};

port_t *pwm2 = &_pwm2;
#endif

#ifdef HAS_PWM3
static port_t _pwm3 = {

    .slot = 3,

    .id = PWM3_ID,
    .type = PORT_TYPE_NUMBER,
    .flags = PORT_FLAG_OUTPUT,

    .min = PWM_MIN,
    .max = PWM_MAX,
    .step = UNDEFINED,
    .integer = TRUE,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure

};

port_t *pwm3 = &_pwm3;
#endif

#ifdef HAS_PWM4
static port_t _pwm4 = {

    .slot = 4,

    .id = PWM4_ID,
    .type = PORT_TYPE_NUMBER,
    .flags = PORT_FLAG_OUTPUT,

    .min = PWM_MIN,
    .max = PWM_MAX,
    .step = UNDEFINED,
    .integer = TRUE,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure

};

port_t *pwm4 = &_pwm4;
#endif

#ifdef HAS_PWM5
static port_t _pwm5 = {

    .slot = 5,

    .id = PWM5_ID,
    .type = PORT_TYPE_NUMBER,
    .flags = PORT_FLAG_OUTPUT,

    .min = PWM_MIN,
    .max = PWM_MAX,
    .step = UNDEFINED,
    .integer = TRUE,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure

};

port_t *pwm5 = &_pwm5;
#endif

#ifdef HAS_PWM12
static port_t _pwm12 = {

    .slot = 12,

    .id = PWM12_ID,
    .type = PORT_TYPE_NUMBER,
    .flags = PORT_FLAG_OUTPUT,

    .min = PWM_MIN,
    .max = PWM_MAX,
    .step = UNDEFINED,
    .integer = TRUE,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure

};

port_t *pwm12 = &_pwm12;
#endif

#ifdef HAS_PWM13
static port_t _pwm13 = {

    .slot = 13,

    .id = PWM13_ID,
    .type = PORT_TYPE_NUMBER,
    .flags = PORT_FLAG_OUTPUT,

    .min = PWM_MIN,
    .max = PWM_MAX,
    .step = UNDEFINED,
    .integer = TRUE,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure

};

port_t *pwm13 = &_pwm13;
#endif

#ifdef HAS_PWM14
static port_t _pwm14 = {

    .slot = 14,

    .id = PWM14_ID,
    .type = PORT_TYPE_NUMBER,
    .flags = PORT_FLAG_OUTPUT,

    .min = PWM_MIN,
    .max = PWM_MAX,
    .step = UNDEFINED,
    .integer = TRUE,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure

};

port_t *pwm14 = &_pwm14;
#endif

#ifdef HAS_PWM15
static port_t _pwm15 = {

    .slot = 15,

    .id = PWM15_ID,
    .type = PORT_TYPE_NUMBER,
    .flags = PORT_FLAG_OUTPUT,

    .min = PWM_MIN,
    .max = PWM_MAX,
    .step = UNDEFINED,
    .integer = TRUE,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure

};

port_t *pwm15 = &_pwm15;
#endif


double freq_read_value(port_t *port) {
    return pwm_cur_freq;
}

bool freq_write_value(port_t *port, double value) {
    pwm_cur_freq = value;
    freq_configure(port);

    return TRUE;
}

void freq_configure(port_t *port) {
    int i;

    DEBUG_PWM(port, "setting pwm frequency to %d Hz", pwm_cur_freq);
    pwm_set_period(10000000 / (pwm_cur_freq * 2));

    /* recalculate duty period for all enabled pwm ports */
    for (i = 0; i < pwm_count; i++) {
        pwm_set_duty(IS_ENABLED(port) ? pwm_cur_duty[i] * pwm_get_period() / 100 : 0, i);
    }

    pwm_start();
}

double read_value(port_t *port) {
    return pwm_cur_duty[port->mapped - 1];
}

bool write_value(port_t *port, double value) {
    pwm_cur_duty[port->mapped - 1] = value;
    configure(port);

    return TRUE;
}

void configure(port_t *port) {
    int duty = (IS_ENABLED(port) && IS_ENABLED(pwm_freq)) ? pwm_cur_duty[port->mapped - 1] : 0;
    DEBUG_PWM(port, "setting duty to %d%%", duty);
    pwm_set_duty(duty * pwm_get_period() / 100, port->mapped - 1);
    pwm_start();
}

void pwm_init_ports(void) {
    uint32 *pwm_duty_list = NULL;
    uint32 (*pwm_info)[3] = NULL;

    port_register(pwm_freq);

#ifdef HAS_PWM0
    PWM_REGISTER(pwm0);

    pwm_count++;
    pwm_info = realloc(pwm_info, sizeof(uint32) * 3 * pwm_count);
    PWM_SET_INFO(pwm_info[pwm_count - 1], FUNC_GPIO0, 0);
#endif

#ifdef HAS_PWM1
    PWM_REGISTER(pwm1);

    pwm_count++;
    pwm_info = realloc(pwm_info, sizeof(uint32) * 3 * pwm_count);
    PWM_SET_INFO(pwm_info[pwm_count - 1], FUNC_GPIO1, 1);
#endif

#ifdef HAS_PWM2
    PWM_REGISTER(pwm2);

    pwm_count++;
    pwm_info = realloc(pwm_info, sizeof(uint32) * 3 * pwm_count);
    PWM_SET_INFO(pwm_info[pwm_count - 1], FUNC_GPIO2, 2);
#endif

#ifdef HAS_PWM3
    PWM_REGISTER(pwm3);

    pwm_count++;
    pwm_info = realloc(pwm_info, sizeof(uint32) * 3 * pwm_count);
    PWM_SET_INFO(pwm_info[pwm_count - 1], FUNC_GPIO3, 3);
#endif

#ifdef HAS_PWM4
    PWM_REGISTER(pwm4);

    pwm_count++;
    pwm_info = realloc(pwm_info, sizeof(uint32) * 3 * pwm_count);
    PWM_SET_INFO(pwm_info[pwm_count - 1], FUNC_GPIO4, 4);
#endif

#ifdef HAS_PWM5
    PWM_REGISTER(pwm5);

    pwm_count++;
    pwm_info = realloc(pwm_info, sizeof(uint32) * 3 * pwm_count);
    PWM_SET_INFO(pwm_info[pwm_count - 1], FUNC_GPIO5, 5);
#endif

#ifdef HAS_PWM12
    PWM_REGISTER(pwm12);

    pwm_count++;
    pwm_info = realloc(pwm_info, sizeof(uint32) * 3 * pwm_count);
    PWM_SET_INFO(pwm_info[pwm_count - 1], FUNC_GPIO12, 12);
#endif

#ifdef HAS_PWM13
    PWM_REGISTER(pwm13);

    pwm_count++;
    pwm_info = realloc(pwm_info, sizeof(uint32) * 3 * pwm_count);
    PWM_SET_INFO(pwm_info[pwm_count - 1], FUNC_GPIO13, 13);
#endif

#ifdef HAS_PWM14
    PWM_REGISTER(pwm14);

    pwm_count++;
    pwm_info = realloc(pwm_info, sizeof(uint32) * 3 * pwm_count);
    PWM_SET_INFO(pwm_info[pwm_count - 1], FUNC_GPIO14, 14);
#endif

#ifdef HAS_PWM15
    PWM_REGISTER(pwm15);

    pwm_count++;
    pwm_info = realloc(pwm_info, sizeof(uint32) * 3 * pwm_count);
    PWM_SET_INFO(pwm_info[pwm_count - 1], FUNC_GPIO15, 15);
#endif

    pwm_duty_list = zalloc(sizeof(uint32) * pwm_count);
    pwm_cur_duty = zalloc(sizeof(char) * pwm_count);

    pwm_init(5000 /* will be overwritten later */, pwm_duty_list, pwm_count, pwm_info);
    pwm_start();
}


#endif  /* HAS_PWM */
