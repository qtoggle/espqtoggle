
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
#include <user_interface.h>
#include <version.h>

#include "espgoodies/common.h"
#include "espgoodies/wifi.h"
#include "espgoodies/crypto.h"
#include "espgoodies/initdata.h"
#include "espgoodies/rtc.h"
#include "espgoodies/system.h"
#include "espgoodies/tcpserver.h"
#include "espgoodies/utils.h"

#ifdef _SLEEP
#include "espgoodies/sleep.h"
#endif

#ifdef _OTA
#include "espgoodies/ota.h"
#endif

#include "client.h"
#include "common.h"
#include "config.h"
#include "core.h"
#include "device.h"
#include "drivers/uart.h"
#include "ver.h"


#define CONNECT_TIMEOUT             20000   /* milliseconds */
#define DEBUG_BAUD                  115200

#define FW_LATEST_FILE              "/latest"
#define FW_LATEST_BETA_FILE         "/latest_beta"
#define FW_AUTO_MIN_INTERVAL        24  /* hours */


static bool                         wifi_first_time_connected = FALSE;
static os_timer_t                   connect_timeout_timer;


ICACHE_FLASH_ATTR static void       main_init(void);
#ifdef _DEBUG
ICACHE_FLASH_ATTR static void       debug_putc_func(char c);
#endif
ICACHE_FLASH_ATTR void              user_init(void);
ICACHE_FLASH_ATTR void              user_rf_pre_init(void);
ICACHE_FLASH_ATTR int               user_rf_cal_sector_set(void);

ICACHE_FLASH_ATTR static void       on_system_ready(void);
ICACHE_FLASH_ATTR static void       on_system_reset(void);

#ifdef _OTA
ICACHE_FLASH_ATTR static void       on_ota_auto_perform(int code);
#endif

ICACHE_FLASH_ATTR static void       on_wifi_connect(bool connected);
ICACHE_FLASH_ATTR static void       on_wifi_connect_timeout(void *arg);


/* main/system */

void main_init(void) {
    system_init_done_cb(on_system_ready);

    os_timer_disarm(&connect_timeout_timer);
    os_timer_setfn(&connect_timeout_timer, on_wifi_connect_timeout, NULL);
    os_timer_arm(&connect_timeout_timer, CONNECT_TIMEOUT, /* repeat = */ FALSE);

    system_set_reset_callback(on_system_reset);
}

void on_system_ready(void) {
    DEBUG_SYSTEM("system initialization done");

    if (wifi_get_ssid()[0]) {
        if (wifi_get_bssid()[0]) { /* specific BSSID set, connect without scan */
            wifi_connect(wifi_get_bssid());
        }
        else {
            wifi_auto_scan();
        }
    }
    else { /* no SSID set, no WiFi network configured */
        DEBUG_SYSTEM("no SSID configured, switching to setup mode");
        if (!system_setup_mode_active()) {
            system_setup_mode_toggle();
        }
    }

    /* generate a device-update event, mainly to be used with webhooks */
    event_push_device_update();
}

void on_system_reset(void) {
    DEBUG_SYSTEM("cleaning up before reset");

    ensure_ports_saved();
    sessions_respond_all();
    tcp_server_stop();
}

#ifdef _OTA
void on_ota_auto_perform(int code) {
    ensure_ports_saved();
    sessions_respond_all();
}
#endif

void on_wifi_connect(bool connected) {
    if (wifi_first_time_connected) {
        return;  /* we're only interested in first time connection */
    }

    if (!connected) {
        return;  /* we don't care about disconnections */
    }

    DEBUG_SYSTEM("we're connected!");

    wifi_first_time_connected = TRUE;
    os_timer_disarm(&connect_timeout_timer);
    core_enable_ports_polling();

#ifdef _SLEEP
    /* start sleep timer now that we're connected */
    sleep_reset();
#endif

#ifdef _OTA
    /* start firmware auto update mechanism */

    if (device_flags & DEVICE_FLAG_OTA_AUTO_UPDATE) {
#ifdef _SLEEP
        int wake_interval = sleep_get_wake_interval();
        if (wake_interval) {
            /* do not check for an update more often than each OTA_AUTO_MIN_INTERVAL hours */
            int sleep_boot_count = rtc_get_boot_count();
            int count_modulo = FW_AUTO_MIN_INTERVAL * 60 / wake_interval;
            if (count_modulo < 1) {  /* for sleep intervals longer than 24h */
                count_modulo = 1;
            }

            DEBUG_SYSTEM("sleep boot count: %d %% %d = %d", sleep_boot_count, count_modulo,
                         sleep_boot_count % count_modulo);

            if (sleep_boot_count % count_modulo == 0) {
                ota_auto_update_check(version_is_beta() || version_is_alpha(), on_ota_auto_perform);
            }
        }
        else {  /* sleep disabled, check for update at each boot */
            ota_auto_update_check(version_is_beta() || version_is_alpha(), on_ota_auto_perform);
        }

#else  /* !_SLEEP */
        ota_auto_update_check(version_is_beta() || version_is_alpha(), on_ota_auto_perform);
#endif
    }

#endif  /* _OTA */
}

void on_wifi_connect_timeout(void *arg) {
    DEBUG_SYSTEM("timeout waiting for initial WiFi connection");
    core_enable_ports_polling();
#ifdef _SLEEP
    sleep_reset();
#endif
}


    /* main functions */

void user_rf_pre_init(void) {
    init_data_ensure();

    system_deep_sleep_set_option(2);    /* no RF calibration after waking from deep sleep */
    system_phy_set_rfoption(2);         /* no RF calibration after waking from deep sleep */
    system_phy_set_powerup_option(2);   /* calibration only for VDD33 and Tx power */
}

int user_rf_cal_sector_set(void) {
    /* always consider a 1MB flash size; set RF_CAL to the 5th last sector */
    return 256 - 5;
}

#ifdef _DEBUG
void debug_putc_func(char c) {
    uart_write_char(_DEBUG_UART_NO, c);
}
#endif

void user_init(void) {
#ifdef _DEBUG
    uart_setup(_DEBUG_UART_NO, DEBUG_BAUD, UART_PARITY_NONE, UART_STOP_BITS_1);
    os_install_putc1(debug_putc_func);
    os_delay_us(10000);

printf("\n\n");
DEBUG("espQToggle  " FW_VERSION);
DEBUG("Config      " FW_CONFIG_NAME);
DEBUG("API Version " API_VERSION);
DEBUG("SDK Version " ESP_SDK_VERSION_STRING);

#else /* !_DEBUG */
    system_set_os_print(0);
#endif /* _DEBUG */

    rtc_init();
#ifdef _SLEEP
    sleep_init();
#endif
    config_init();
#ifdef _OTA
    ota_init(/* current_version = */ FW_VERSION,
             /* url = */             FW_BASE_URL FW_BASE_OTA_PATH "/" FW_CONFIG_NAME FW_LATEST_FILE,
             /* beta_url = */        FW_BASE_URL FW_BASE_OTA_PATH "/" FW_CONFIG_NAME FW_LATEST_BETA_FILE,
             /* url_template = */    FW_BASE_URL FW_BASE_OTA_PATH "/" FW_CONFIG_NAME "/%s");
#endif
    wifi_set_station_mode(on_wifi_connect, device_hostname);
    client_init();
    core_init();
    system_init();
    main_init();
}
