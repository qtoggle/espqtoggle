
/*
 * Copyright (c) Calin Crisan
 * This file is part of espQToggle.
 *
 * espQToggle is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>. 
 */

#include <stdlib.h>
#include <string.h>

#include <mem.h>
#include <user_interface.h>
#include <version.h>

#ifdef _GDB
#include <gdbstub.h>
#endif

#include "espgoodies/common.h"
#include "espgoodies/wifi.h"
#include "espgoodies/crypto.h"
#include "espgoodies/system.h"
#include "espgoodies/tcpserver.h"
#include "espgoodies/utils.h"
#include "espgoodies/pingwdt.h"
#include "espgoodies/rtc.h"

#ifdef _SLEEP
#include "espgoodies/sleep.h"
#endif

#ifdef _OTA
#include "espgoodies/ota.h"
#endif

#include "config.h"
#include "client.h"
#include "device.h"
#include "core.h"
#include "ver.h"


#define CONNECT_TIMEOUT             20000   /* milliseconds */

#define FW_BASE_URL                 "http://firmware.qtoggle.com"
#define FW_BASE_PATH                "/espqtoggle"
#define FW_LATEST_FILE              "/latest"
#define FW_LATEST_BETA_FILE         "/latest_beta"
#define FW_AUTO_MIN_INTERVAL        24  /* hours */


static bool                         wifi_first_time_connected = FALSE;
static os_timer_t                   connect_timeout_timer;


ICACHE_FLASH_ATTR static void       main_init();

ICACHE_FLASH_ATTR static void       on_system_ready();
ICACHE_FLASH_ATTR static void       on_system_reset();

#ifdef _OTA
ICACHE_FLASH_ATTR static void       on_ota_auto_perform(int code);
#endif

ICACHE_FLASH_ATTR static void       on_wifi_connect(bool connected);
ICACHE_FLASH_ATTR static void       on_connect_timeout(void *arg);


/* main/system */

void main_init() {
    system_init_done_cb(on_system_ready);

    os_timer_disarm(&connect_timeout_timer);
    os_timer_setfn(&connect_timeout_timer, on_connect_timeout, NULL);
    os_timer_arm(&connect_timeout_timer, CONNECT_TIMEOUT, /* repeat = */ FALSE);

    system_set_reset_callback(on_system_reset);
}

void on_system_ready() {
    DEBUG_SYSTEM("system initialization done");

    if (wifi_get_bssid()[0]) {
        wifi_connect(wifi_get_bssid());
    }
    else if (wifi_get_ssid()[0]) {
        wifi_auto_scan();
    }
    else {
        DEBUG_SYSTEM("no SSID configured, switching to setup mode");
        if (!system_setup_mode_active()) {
            system_setup_mode_toggle();
        }
    }

    /* generate a device-update event, mainly to be used with webhooks */
    event_push_device_update();
}

void on_system_reset() {
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

    wifi_first_time_connected = TRUE;

    DEBUG_SYSTEM("we're connected!");

    os_timer_disarm(&connect_timeout_timer);

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

void on_connect_timeout(void *arg) {
    DEBUG_SYSTEM("timeout waiting for initial WiFi connection");
#ifdef _SLEEP
    sleep_reset();
#endif
}


    /* main functions */

void user_rf_pre_init() {
    system_deep_sleep_set_option(2);    /* no RF calibration after waking from deep sleep */
    system_phy_set_rfoption(2);         /* no RF calibration after waking from deep sleep */
    system_phy_set_powerup_option(2);   /* calibration only for VDD33 and Tx power */
}

int user_rf_cal_sector_set(void) {
    switch (system_get_flash_size_map()) {
        case FLASH_SIZE_4M_MAP_256_256:
            return 128 - 5;

        case FLASH_SIZE_8M_MAP_512_512:
            return 256 - 5;

        default:
            return 512 - 5;
    }
}

void user_init() {
#ifdef _DEBUG
    uart_div_modify(0, UART_CLK_FREQ / 115200);
    os_delay_us(10000);

#ifdef _GDB
    gdbstub_init();
#else
    printf("\n\n");
    DEBUG("espQToggle  " FW_VERSION);
    DEBUG("API Version " API_VERSION);
    DEBUG("SDK Version " ESP_SDK_VERSION_STRING);
#endif /* _GDB */

#else /* !_DEBUG */
    system_set_os_print(0);
#endif /* _DEBUG */

    rtc_init();
#ifdef _SLEEP
    sleep_init();
#endif
    config_init();
#ifdef _OTA
    ota_init(/* current_version = */    FW_VERSION,
             /* url = */                FW_BASE_URL FW_BASE_PATH "/" FW_CONFIG_ID FW_LATEST_FILE,
             /* beta_url = */           FW_BASE_URL FW_BASE_PATH "/" FW_CONFIG_ID FW_LATEST_BETA_FILE,
             /* url_template = */       FW_BASE_URL FW_BASE_PATH "/" FW_CONFIG_ID "/%s");
#endif
    wifi_set_station_mode(on_wifi_connect, device_hostname);
    client_init();
    ping_wdt_init();
    core_init();
    system_init();
    main_init();
}
