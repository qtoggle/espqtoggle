
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

#include "battery.h"
#include "common.h"
#include "dnsserver.h"
#include "flashcfg.h"
#include "gpio.h"
#include "ota.h"
#include "rtc.h"
#include "tcpserver.h"
#include "wifi.h"
#include "system.h"


#define RESET_DELAY          3000    /* Milliseconds */

#define SETUP_MODE_IDLE      0
#define SETUP_MODE_PRESSED   1
#define SETUP_MODE_TRIGGERED 2
#define SETUP_MODE_RESET     3

static int8                         setup_button_pin = -1;
static bool                         setup_button_level = FALSE;
static uint8                        setup_button_hold = 5;
static uint8                        setup_button_reset_hold = 20;

static int8                         status_led_pin = -1;
static bool                         status_led_level = TRUE;

static uint32                       last_time_us = 0;
static uint64                       uptime_us = 0;
static os_timer_t                   reset_timer;
static system_reset_callback_t      reset_callback = NULL;
static system_setup_mode_callback_t setup_mode_callback;

static uint64                       setup_mode_time = 0;
static uint8                        setup_mode_state = SETUP_MODE_IDLE;
static bool                         setup_mode = FALSE;

static uint32                       fw_version_int = 0;


ICACHE_FLASH_ATTR static void on_system_reset(void *arg);


void system_config_init(void) {
    /* Load system config from flash */
    uint8 *config_data = zalloc(FLASH_CONFIG_SIZE_SYSTEM);
    flashcfg_load(FLASH_CONFIG_SLOT_SYSTEM, config_data);

    /* If config data is full of 0xFF, that usually indicates erased flash. Fill it with 0s in that case, which is what
     * we use for default config. */
    uint16 i;
    bool erased = TRUE;
    for (i = 0; i < 32; i++) {
        if (config_data[i] != 0xFF) {
            erased = FALSE;
            break;
        }
    }

    if (erased) {
        DEBUG_SYSTEM("detected erased flash config");
        memset(config_data, 0, FLASH_CONFIG_SIZE_SYSTEM);
        flashcfg_save(FLASH_CONFIG_SLOT_SYSTEM, config_data);
    }

    /* If config data is full of 0x00, that indicates default config */
    bool null = TRUE;
    for (i = 0; i < 32; i++) {
        if (config_data[i] != 0) {
            null = FALSE;
            break;
        }
    }

    if (null) {
        DEBUG_SYSTEM("detected default flash config");
        /* Leave settings as they are initialized */
    }
    else {
        setup_button_pin = config_data[SYSTEM_CONFIG_OFFS_SETUP_BUTTON_PIN];
        setup_button_level = config_data[SYSTEM_CONFIG_OFFS_SETUP_BUTTON_LEVEL];
        setup_button_hold = config_data[SYSTEM_CONFIG_OFFS_SETUP_BUTTON_HOLD];
        setup_button_reset_hold = config_data[SYSTEM_CONFIG_OFFS_SETUP_BUTTON_HOLDR];

        status_led_pin = config_data[SYSTEM_CONFIG_OFFS_STATUS_LED_PIN];
        status_led_level = config_data[SYSTEM_CONFIG_OFFS_STATUS_LED_LEVEL];

        memcpy(&fw_version_int, config_data + SYSTEM_CONFIG_OFFS_FW_VERSION, 4);
    }

#if defined(_DEBUG_SYSTEM) && defined(_DEBUG)
    version_t fw_version;
    system_get_fw_version(&fw_version);
    char *version_str = version_dump(&fw_version);
    DEBUG_SYSTEM("loaded FW version = %s", version_str);
    free(version_str);
#endif

    DEBUG_SYSTEM(
        "loaded setup button: pin = %d, level = %d, hold = %d, reset_hold = %d",
        setup_button_pin,
        setup_button_level,
        setup_button_hold,
        setup_button_reset_hold
    );

    DEBUG_SYSTEM("loaded status LED: pin = %d, level = %d", status_led_pin, status_led_level);

    if (setup_button_pin >= 0) {
        gpio_configure_input(setup_button_pin, !setup_button_level);
        if (gpio_read_value(setup_button_pin) == setup_button_level) {
            DEBUG_SYSTEM("setup button active at boot time, disabling");
            setup_button_pin = -1;
        }
    }
    if (status_led_pin >= 0) {
        gpio_configure_output(status_led_pin, !status_led_level);
    }

#ifdef _BATTERY
    battery_config_init(config_data);
#endif

    free(config_data);
}

void system_config_save(void) {
    /* Save system config to flash */

    uint8 *config_data = zalloc(FLASH_CONFIG_SIZE_SYSTEM);
    flashcfg_load(FLASH_CONFIG_SLOT_SYSTEM, config_data);

    config_data[SYSTEM_CONFIG_OFFS_SETUP_BUTTON_PIN] = setup_button_pin;
    config_data[SYSTEM_CONFIG_OFFS_SETUP_BUTTON_LEVEL] = setup_button_level;
    config_data[SYSTEM_CONFIG_OFFS_SETUP_BUTTON_HOLD] = setup_button_hold;
    config_data[SYSTEM_CONFIG_OFFS_SETUP_BUTTON_HOLDR] = setup_button_reset_hold;

    config_data[SYSTEM_CONFIG_OFFS_STATUS_LED_PIN] = status_led_pin;
    config_data[SYSTEM_CONFIG_OFFS_STATUS_LED_LEVEL] = status_led_level;

    memcpy(config_data + SYSTEM_CONFIG_OFFS_FW_VERSION, &fw_version_int, 4);

#ifdef _BATTERY
    battery_config_save(config_data);
#endif

    flashcfg_save(FLASH_CONFIG_SLOT_SYSTEM, config_data);
    free(config_data);
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

void system_reset_set_callback(system_reset_callback_t callback) {
    reset_callback = callback;
}

void system_get_fw_version(version_t *version) {
    version_from_int(version, fw_version_int);
}

void system_set_fw_version(version_t *version) {
    fw_version_int = version_to_int(version);

#if defined(_DEBUG_SYSTEM) && defined(_DEBUG)
    char *version_str = version_dump(version);
    DEBUG_SYSTEM("setting FW version = %s", version_str);
    free(version_str);
#endif
}

void system_setup_button_set_config(int8 pin, bool level, uint8 hold, uint8 reset_hold) {
    DEBUG_SYSTEM(
        "configuring setup button: pin = %d, level = %d, hold = %d, reset_hold = %d",
        pin,
        level,
        hold,
        reset_hold
    );

    setup_button_pin = pin;
    setup_button_level = level;
    setup_button_hold = hold;
    setup_button_reset_hold = reset_hold;

    if (setup_button_pin >= 0) {
        gpio_configure_input(setup_button_pin, !setup_button_level);
    }
}

void system_setup_button_get_config(int8 *pin, bool *level, uint8 *hold, uint8 *reset_hold) {
    *pin = setup_button_pin;
    *level = setup_button_level;
    *hold = setup_button_hold;
    *reset_hold = setup_button_reset_hold;
}


void system_status_led_set_config(int8 pin, bool level) {
    DEBUG_SYSTEM("configuring status LED: pin = %d, level = %d", pin, level);

    status_led_pin = pin;
    status_led_level = level;

    if (status_led_pin >= 0) {
        gpio_configure_output(status_led_pin, !status_led_level);
    }
}

void system_status_led_get_config(int8 *pin, bool *level) {
    *pin = status_led_pin;
    *level = status_led_level;
}

void system_setup_mode_set_callback(system_setup_mode_callback_t callback) {
    setup_mode_callback = callback;
}

bool system_setup_mode_active(void) {
    return setup_mode;
}

void system_setup_mode_toggle(void) {
    if (setup_mode) {
        DEBUG_SYSTEM("exiting setup mode");

        if (setup_mode_callback) {
            setup_mode_callback(FALSE);
        }

        system_reset(/* delayed = */ FALSE);
    }
    else {
        setup_mode = TRUE;

        DEBUG_SYSTEM("entering setup mode");

        char ssid[WIFI_SSID_MAX_LEN + 1];
        snprintf(ssid, sizeof(ssid), DEFAULT_HOSTNAME, system_get_chip_id());
        wifi_ap_enable(ssid, /* psk = */ NULL, NULL);
        dnsserver_start_captive();

        if (setup_mode_callback) {
            setup_mode_callback(TRUE);
        }
    }
}

void system_update(void) {
    uint64 now_us = system_uptime_us();
    uint64 now_ms = now_us / 1000;

    /* If system reset has been triggered, stop polling setup button/blinking status LED */
    if (setup_mode_state == SETUP_MODE_RESET) {
        return;
    }

    /* Do a factory reset if setup button was held pressed enough */
    if (setup_mode_time > 0 && now_ms - setup_mode_time > setup_button_reset_hold * 1000) {
        DEBUG_SYSTEM("resetting to factory defaults");
        setup_mode_state = SETUP_MODE_RESET;

        flashcfg_reset(FLASH_CONFIG_SLOT_DEFAULT);
        wifi_reset();

        system_reset(/* delayed = */ TRUE);
        return;
    }

    /* Enter setup mode if setup button was held pressed enough */
    if (setup_mode_state == SETUP_MODE_PRESSED && now_ms - setup_mode_time > setup_button_hold * 1000) {
        setup_mode_state = SETUP_MODE_TRIGGERED;
        system_setup_mode_toggle();
    }

    /* Read setup mode pin state */
    if (setup_button_pin >= 0) {
        bool value = gpio_read_value(setup_button_pin);
        if (value == setup_button_level && setup_mode_state == SETUP_MODE_IDLE) {
            DEBUG_SYSTEM("setup mode timer started");
            setup_mode_time = now_ms;
            setup_mode_state = SETUP_MODE_PRESSED;
        }
        else if (value != setup_button_level && setup_mode_state != SETUP_MODE_IDLE) {
            DEBUG_SYSTEM("setup mode timer reset");
            setup_mode_time = 0;
            setup_mode_state = SETUP_MODE_IDLE;
        }
    }

    /* Blink the status LED according to OTA state, setup mode and Wi-Fi connection */
    if (status_led_pin >= 0) {
        int ota_state = ota_current_state();
        bool ota_active = ota_state == OTA_STATE_DOWNLOADING || ota_state == OTA_STATE_RESTARTING;
        bool current_led_value = gpio_read_value(status_led_pin);
        bool new_led_value = current_led_value;

        if (ota_active || setup_mode) {
            new_led_value = (now_us * 7 / 1000000) % 2;
        }
        else {
            if (!wifi_station_is_connected()) {
                new_led_value = (now_us * 2 / 1000000) % 2;
            }
            else {
                new_led_value = !status_led_level;
            }
        }

        if (current_led_value != new_led_value) {
            gpio_write_value(status_led_pin, new_led_value);
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
