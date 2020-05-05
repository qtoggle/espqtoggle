
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
#include <limits.h>
#include <user_interface.h>
#include <gpio.h>

#include "common.h"
#include "dnsserver.h"
#include "flashcfg.h"
#include "gpio.h"
#include "ota.h"
#include "rtc.h"
#include "tcpserver.h"
#include "wifi.h"
#include "system.h"


#define RESET_DELAY                 3000    /* Milliseconds */

#define SETUP_MODE_IDLE             0
#define SETUP_MODE_PRESSED          1
#define SETUP_MODE_TOGGLED          2
#define SETUP_MODE_RESET            3

#define _STRING1(x)                 #x
#define _STRING(x)                  _STRING1(x)

int8                                system_setup_mode_gpio_no = -1;
int8                                system_setup_mode_led_gpio_no = -1;
int32                               system_setup_mode_int = 10;
int32                               system_setup_mode_reset_int = 20;
int8                                system_connected_led_gpio_no = -1;

static uint32                       last_time_us = 0;
static uint64                       uptime_us = 0;
static os_timer_t                   reset_timer;
static bool                         setup_mode = FALSE;
static system_reset_callback_t      reset_callback = NULL;

static int                          setup_mode_timer = 0;
static int                          setup_mode_state = SETUP_MODE_IDLE;


ICACHE_FLASH_ATTR static void       on_system_reset(void *arg);


void system_init(void) {
#ifdef SETUP_MODE_PORT
    /* Skip "gpio" and convert the rest to number */
    system_setup_mode_gpio_no = strtol(_STRING(SETUP_MODE_PORT) + 4, NULL, 10);
    gpio_configure_input(system_setup_mode_gpio_no, !SETUP_MODE_LEVEL);
    DEBUG_SYSTEM("setup mode GPIO set to %d", system_setup_mode_gpio_no);
    DEBUG_SYSTEM("setup mode level set to %d", SETUP_MODE_LEVEL);

    system_setup_mode_int = SETUP_MODE_INT;
    DEBUG_SYSTEM("setup mode interval set to %d", system_setup_mode_int);

    system_setup_mode_reset_int = SETUP_MODE_RESET_INT;
    DEBUG_SYSTEM("setup mode reset interval set to %d", system_setup_mode_reset_int);

#ifdef SETUP_MODE_LED_PORT
    system_setup_mode_led_gpio_no = strtol(_STRING(SETUP_MODE_LED_PORT) + 4, NULL, 10);
    gpio_configure_output(system_setup_mode_led_gpio_no, FALSE);
    DEBUG_SYSTEM("setup mode led GPIO set to %d", system_setup_mode_led_gpio_no);
#endif

#endif

#ifdef CONNECTED_LED_PORT
    system_connected_led_gpio_no = strtol(_STRING(CONNECTED_LED_PORT) + 4, NULL, 10);
    gpio_configure_output(system_connected_led_gpio_no, !CONNECTED_LED_LEVEL);
    DEBUG_SYSTEM("connected led GPIO set to %d", system_connected_led_gpio_no);
    DEBUG_SYSTEM("connected level set to %d", CONNECTED_LED_LEVEL);
#endif
}

uint32 system_uptime(void) {
    /* Call system_uptime_us() to update the internal uptime value */
    system_uptime_us();

    return uptime_us / 1000000;
}

uint64 system_uptime_ms(void) {
    /* Call system_uptime_us() to update the internal uptime value */
    system_uptime_us();

    return uptime_us / 1000;
}

uint64 system_uptime_us(void) {
    int64 time = system_get_time();
    if (time - last_time_us < -1000000) { /* Time overflow */
        uptime_us += time + UINT_MAX - last_time_us;
    }
    else {
        uptime_us += time - last_time_us;
    }

    last_time_us = time;

    return uptime_us;
}

int system_get_flash_size(void) {
    return (1UL << ((spi_flash_get_id() >> 16) & 0xff));
}

void system_reset(bool delayed) {
    if (reset_callback) {
        reset_callback();
    }

    if (delayed) {
        DEBUG_SYSTEM("will reset in %d seconds", RESET_DELAY / 1000);

        os_timer_disarm(&reset_timer);
        os_timer_setfn(&reset_timer, on_system_reset, NULL);
        os_timer_arm(&reset_timer, RESET_DELAY, /* repeat = */ FALSE);
    }
    else {
        on_system_reset(NULL);
    }
}

void system_set_reset_callback(system_reset_callback_t callback) {
    reset_callback = callback;
}

bool system_setup_mode_active(void) {
    return setup_mode;
}

void system_setup_mode_toggle(void) {
    if (setup_mode) {
        DEBUG_SYSTEM("exiting setup mode");

        system_reset(/* delayed = */ FALSE);
    }
    else {
        setup_mode = TRUE;

        DEBUG_SYSTEM("entering setup mode");

        char ssid[WIFI_SSID_MAX_LEN + 1];
        snprintf(ssid, sizeof(ssid), DEFAULT_HOSTNAME, system_get_chip_id());
        wifi_ap_enable(ssid, /* psk = */ NULL);
        dnsserver_start_captive();
    }
}

void system_setup_mode_update(void) {
    uint64 now_us = system_uptime_us();
    uint32 now = now_us / 1000000;

#ifdef SETUP_MODE_PORT
    if (system_setup_mode_gpio_no != -1) {
        /* Read setup mode port state */
        bool value = gpio_read_value(system_setup_mode_gpio_no);
        if (value == SETUP_MODE_LEVEL && setup_mode_state == SETUP_MODE_IDLE) {
            DEBUG_SYSTEM("setup mode timer started");
            setup_mode_timer = now;
            setup_mode_state = SETUP_MODE_PRESSED;
        }
        else if (value != SETUP_MODE_LEVEL && setup_mode_state != SETUP_MODE_IDLE) {
            DEBUG_SYSTEM("setup mode timer reset");
            setup_mode_timer = 0;
            setup_mode_state = SETUP_MODE_IDLE;
        }
    }
#endif

    /* Check setup mode & reset pin */
    if (setup_mode_state != SETUP_MODE_IDLE) {
        if (setup_mode_state != SETUP_MODE_RESET && now - setup_mode_timer > system_setup_mode_reset_int) {
            DEBUG_SYSTEM("resetting to factory defaults");
            setup_mode_state = SETUP_MODE_RESET;

            flashcfg_reset();
            wifi_reset();

            system_reset(/* delayed = */ TRUE);
        }
        else if (setup_mode_state == SETUP_MODE_PRESSED && now - setup_mode_timer > system_setup_mode_int) {
            setup_mode_state = SETUP_MODE_TOGGLED;
            system_setup_mode_toggle();
        }
    }

    if (system_setup_mode_led_gpio_no != -1) {
        /* Blink the setup mode led */
        int ota_state = ota_current_state();
        if (setup_mode || ota_state == OTA_STATE_DOWNLOADING || ota_state == OTA_STATE_RESTARTING) {
            bool old_blink_value = gpio_read_value(system_setup_mode_led_gpio_no);
            bool new_blink_value = (now_us * 6 / 1000000) % 2;
            if (old_blink_value != new_blink_value) {
                gpio_write_value(system_setup_mode_led_gpio_no, new_blink_value);
            }
        }
    }
}

void system_connected_led_update(void) {
    static bool old_led_level = FALSE;

    if ((system_connected_led_gpio_no == system_setup_mode_led_gpio_no) && setup_mode) {
        /* If we use the same led for both connected status and setup mode, the setup mode led update takes
         * precedence */
        return;
    }

    if (system_connected_led_gpio_no != -1) {
        /* Blink the connected status led if not connected */
        if (!wifi_station_is_connected()) {
            bool new_led_level = (system_uptime_us() * 2 / 1000000) % 2;
            if (old_led_level != new_led_level) {
                gpio_write_value(system_connected_led_gpio_no, new_led_level);
                old_led_level = new_led_level;
            }
        }
        else {
            if (old_led_level != CONNECTED_LED_LEVEL) {
                gpio_write_value(system_connected_led_gpio_no, CONNECTED_LED_LEVEL);
                old_led_level = CONNECTED_LED_LEVEL;
            }
        }
    }
}


void on_system_reset(void *arg) {
    DEBUG_SYSTEM("resetting");

    /* This will save configuration only if changed */
    wifi_save_config();
    rtc_reset();
    system_restart();
}
