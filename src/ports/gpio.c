
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
#include <gpio.h>

#include "espgoodies/common.h"
#include "espgoodies/utils.h"

#include "ports/gpio.h"


#ifdef HAS_GPIO


ICACHE_FLASH_ATTR static double     read_value(port_t *port);
ICACHE_FLASH_ATTR static bool       write_value(port_t *port, double value);
ICACHE_FLASH_ATTR static void       configure(port_t *port);

ICACHE_FLASH_ATTR static bool       attr_get_pull_up(port_t *port, attrdef_t *attrdef);
ICACHE_FLASH_ATTR static void       attr_set_pull_up(port_t *port, attrdef_t *attrdef, bool value);

#ifdef HAS_GPIO16
ICACHE_FLASH_ATTR static bool       attr_get_pull_down(port_t *port, attrdef_t *attrdef);
ICACHE_FLASH_ATTR static void       attr_set_pull_down(port_t *port, attrdef_t *attrdef, bool value);
#endif

ICACHE_FLASH_ATTR static bool       attr_get_output(port_t *port, attrdef_t *attrdef);
ICACHE_FLASH_ATTR static void       attr_set_output(port_t *port, attrdef_t *attrdef, bool value);


typedef struct {

    bool                            output_value;  /* used for output buffering */

} extra_info_t;


#define get_output_value(p)         (((extra_info_t *) (p)->extra_info)->output_value)
#define set_output_value(p, v)      {((extra_info_t *) (p)->extra_info)->output_value = v;}


static attrdef_t pull_up_attrdef = {

    .name = "pull_up",
    .display_name = "Pull-Up",
    .description = "Enables the internal pull-up.",
    .type = ATTR_TYPE_BOOLEAN,
    .modifiable = TRUE,
    .set = attr_set_pull_up,
    .get = attr_get_pull_up

};

#ifdef HAS_GPIO16
static attrdef_t pull_down_attrdef = {

    .name = "pull_down",
    .display_name = "Pull-Down",
    .description = "Enables the internal pull-down.",
    .type = ATTR_TYPE_BOOLEAN,
    .modifiable = TRUE,
    .set = attr_set_pull_down,
    .get = attr_get_pull_down

};
#endif

static attrdef_t output_attrdef = {

    .name = "output",
    .display_name = "Output Mode",
    .description = "Makes this GPIO an output pin.",
    .type = ATTR_TYPE_BOOLEAN,
    .modifiable = TRUE,
    .set = attr_set_output,
    .get = attr_get_output

};

static attrdef_t *attrdefs[] = {

    &output_attrdef,
    &pull_up_attrdef,
    NULL

};

#ifdef HAS_GPIO16
static attrdef_t *attrdefs_gpio16[] = {

    &output_attrdef,
    &pull_down_attrdef,
    NULL

};
#endif


#ifdef HAS_GPIO0
static extra_info_t gpio0_extra_info;

static port_t _gpio0 = {

    .slot = 0,

    .id = GPIO0_ID,
    .type = PORT_TYPE_BOOLEAN,
    
    .read_value = read_value,
    .write_value = write_value,
    .configure = configure,

    .extra_info = &gpio0_extra_info,
    .attrdefs = attrdefs

};

port_t *gpio0 = &_gpio0;
#endif

#ifdef HAS_GPIO1
static extra_info_t gpio1_extra_info;

static port_t _gpio1 = {

    .slot = 1,

    .id = GPIO1_ID,
    .type = PORT_TYPE_BOOLEAN,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure,

    .extra_info = &gpio1_extra_info,
    .attrdefs = attrdefs

};

port_t *gpio1 = &_gpio1;
#endif

#ifdef HAS_GPIO2
static extra_info_t gpio2_extra_info;

static port_t _gpio2 = {

    .slot = 2,

    .id = GPIO2_ID,
    .type = PORT_TYPE_BOOLEAN,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure,

    .extra_info = &gpio2_extra_info,
    .attrdefs = attrdefs

};

port_t *gpio2 = &_gpio2;
#endif

#ifdef HAS_GPIO3
static extra_info_t gpio3_extra_info;

static port_t _gpio3 = {

    .slot = 3,

    .id = GPIO3_ID,
    .type = PORT_TYPE_BOOLEAN,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure,

    .extra_info = &gpio3_extra_info,
    .attrdefs = attrdefs

};

port_t *gpio3 = &_gpio3;
#endif

#ifdef HAS_GPIO4
static extra_info_t gpio4_extra_info;

static port_t _gpio4 = {

    .slot = 4,

    .id = GPIO4_ID,
    .type = PORT_TYPE_BOOLEAN,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure,

    .extra_info = &gpio4_extra_info,
    .attrdefs = attrdefs

};

port_t *gpio4 = &_gpio4;
#endif

#ifdef HAS_GPIO5
static extra_info_t gpio5_extra_info;

static port_t _gpio5 = {

    .slot = 5,

    .id = GPIO5_ID,
    .type = PORT_TYPE_BOOLEAN,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure,

    .extra_info = &gpio5_extra_info,
    .attrdefs = attrdefs

};

port_t *gpio5 = &_gpio5;
#endif

#ifdef HAS_GPIO6
static extra_info_t gpio6_extra_info;

static port_t _gpio6 = {

    .slot = 6,

    .id = GPIO6_ID,
    .type = PORT_TYPE_BOOLEAN,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure,

    .extra_info = &gpio6_extra_info,
    .attrdefs = attrdefs

};

port_t *gpio6 = &_gpio6;
#endif

#ifdef HAS_GPIO7
static extra_info_t gpio7_extra_info;

static port_t _gpio7 = {

    .slot = 7,

    .id = GPIO7_ID,
    .type = PORT_TYPE_BOOLEAN,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure,

    .extra_info = &gpio7_extra_info,
    .attrdefs = attrdefs

};

port_t *gpio7 = &_gpio7;
#endif

#ifdef HAS_GPIO8
static extra_info_t gpio8_extra_info;

static port_t _gpio8 = {

    .slot = 8,

    .id = GPIO8_ID,
    .type = PORT_TYPE_BOOLEAN,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure,

    .extra_info = &gpio8_extra_info,
    .attrdefs = attrdefs

};

port_t *gpio8 = &_gpio8;
#endif

#ifdef HAS_GPIO9
static extra_info_t gpio9_extra_info;

static port_t _gpio9 = {

    .slot = 9,

    .id = GPIO9_ID,
    .type = PORT_TYPE_BOOLEAN,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure,

    .extra_info = &gpio9_extra_info,
    .attrdefs = attrdefs

};

port_t *gpio9 = &_gpio9;
#endif

#ifdef HAS_GPIO10
static extra_info_t gpio10_extra_info;

static port_t _gpio10 = {

    .slot = 10,

    .id = GPIO10_ID,
    .type = PORT_TYPE_BOOLEAN,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure,

    .extra_info = &gpio10_extra_info,
    .attrdefs = attrdefs

};

port_t *gpio10 = &_gpio10;
#endif

#ifdef HAS_GPIO11
static extra_info_t gpio11_extra_info;

static port_t _gpio11 = {

    .slot = 11,

    .id = GPIO11_ID,
    .type = PORT_TYPE_BOOLEAN,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure,

    .extra_info = &gpio11_extra_info,
    .attrdefs = attrdefs

};

port_t *gpio11 = &_gpio11;
#endif

#ifdef HAS_GPIO12
static extra_info_t gpio12_extra_info;

static port_t _gpio12 = {

    .slot = 12,

    .id = GPIO12_ID,
    .type = PORT_TYPE_BOOLEAN,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure,

    .extra_info = &gpio12_extra_info,
    .attrdefs = attrdefs

};

port_t *gpio12 = &_gpio12;
#endif

#ifdef HAS_GPIO13
static extra_info_t gpio13_extra_info;

static port_t _gpio13 = {

    .slot = 13,

    .id = GPIO13_ID,
    .type = PORT_TYPE_BOOLEAN,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure,

    .extra_info = &gpio13_extra_info,
    .attrdefs = attrdefs

};

port_t *gpio13 = &_gpio13;
#endif

#ifdef HAS_GPIO14
static extra_info_t gpio14_extra_info;

static port_t _gpio14 = {

    .slot = 14,

    .id = GPIO14_ID,
    .type = PORT_TYPE_BOOLEAN,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure,

    .extra_info = &gpio14_extra_info,
    .attrdefs = attrdefs

};

port_t *gpio14 = &_gpio14;
#endif

#ifdef HAS_GPIO15
static extra_info_t gpio15_extra_info;

static port_t _gpio15 = {

    .slot = 15,

    .id = GPIO15_ID,
    .type = PORT_TYPE_BOOLEAN,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure,

    .extra_info = &gpio15_extra_info,
    .attrdefs = attrdefs

};

port_t *gpio15 = &_gpio15;
#endif

#ifdef HAS_GPIO16
static extra_info_t gpio16_extra_info;

static port_t _gpio16 = {

    .slot = 16,

    .id = GPIO16_ID,
    .type = PORT_TYPE_BOOLEAN,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure,

    .extra_info = &gpio16_extra_info,
    .attrdefs = attrdefs_gpio16

};

port_t *gpio16 = &_gpio16;
#endif

double read_value(port_t *port) {
    if (IS_OUTPUT(port)) {
        return get_output_value(port);
    }
    else {
        return gpio_read_value(port->slot);
    }
}

bool write_value(port_t *port, double value) {
    DEBUG_GPIO(port, "writing %d", !!value);
    gpio_write_value(port->slot, (int) value);

    set_output_value(port, value);

    return TRUE;
}

void configure(port_t *port) {
    bool value;
    if (port->slot == 16) {
        if (IS_PULL_DOWN(port)) {
            DEBUG_GPIO(port, "pull-down enabled");
            value = FALSE;
        }
        else {
            DEBUG_GPIO(port, "pull-down disabled");
            value = TRUE;
        }
    }
    else {
        if (IS_PULL_UP(port)) {
            DEBUG_GPIO(port, "pull-up enabled");
            value = TRUE;
        }
        else {
            DEBUG_GPIO(port, "pull-up disabled");
            value = FALSE;
        }
    }

    if (IS_OUTPUT(port)) {
        DEBUG_GPIO(port, "output enabled");

        /* set initial value according to pull configuration */
        gpio_configure_output(port->slot, value);
        set_output_value(port, value);
    }
    else {
        DEBUG_GPIO(port, "output disabled");
        gpio_configure_input(port->slot, value);
    }
}

bool attr_get_pull_up(port_t *port, attrdef_t *attrdef) {
    return IS_PULL_UP(port);
}

void attr_set_pull_up(port_t *port, attrdef_t *attrdef, bool value) {
    if (value) {
        port->flags |= PORT_FLAG_PULL_UP;
    }
    else {
        port->flags &= ~PORT_FLAG_PULL_UP;
    }
}

#ifdef HAS_GPIO16

bool attr_get_pull_down(port_t *port, attrdef_t *attrdef) {
    return IS_PULL_DOWN(port);
}

void attr_set_pull_down(port_t *port, attrdef_t *attrdef, bool value) {
    if (value) {
        port->flags |= PORT_FLAG_PULL_DOWN;
    }
    else {
        port->flags &= ~PORT_FLAG_PULL_DOWN;
    }
}

#endif /* HAS_GPIO16 */

bool attr_get_output(port_t *port, attrdef_t *attrdef) {
    return IS_OUTPUT(port);
}

void attr_set_output(port_t *port, attrdef_t *attrdef, bool value) {
    if (value) {
        port->flags |= PORT_FLAG_OUTPUT;
    }
    else {
        port->flags &= ~PORT_FLAG_OUTPUT;
    }
}


void gpio_init_ports(void) {

#ifdef HAS_GPIO0
    port_register(gpio0);
#endif

#ifdef HAS_GPIO1
    port_register(gpio1);
#endif

#ifdef HAS_GPIO2
    port_register(gpio2);
#endif

#ifdef HAS_GPIO3
    port_register(gpio3);
#endif

#ifdef HAS_GPIO4
    port_register(gpio4);
#endif

#ifdef HAS_GPIO5
    port_register(gpio5);
#endif

#ifdef HAS_GPIO6
    port_register(gpio6);
#endif

#ifdef HAS_GPIO7
    port_register(gpio7);
#endif

#ifdef HAS_GPIO8
    port_register(gpio8);
#endif

#ifdef HAS_GPIO9
    port_register(gpio9);
#endif

#ifdef HAS_GPIO10
    port_register(gpio10);
#endif

#ifdef HAS_GPIO11
    port_register(gpio11);
#endif

#ifdef HAS_GPIO12
    port_register(gpio12);
#endif

#ifdef HAS_GPIO13
    port_register(gpio13);
#endif

#ifdef HAS_GPIO14
    port_register(gpio14);
#endif

#ifdef HAS_GPIO15
    port_register(gpio15);
#endif

#ifdef HAS_GPIO16
    port_register(gpio16);
#endif

}


#endif  /* HAS_GPIO */
