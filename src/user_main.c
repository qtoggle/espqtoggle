
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
#include <spi_flash.h>

#include "espgoodies/common.h"
#include "espgoodies/crypto.h"
#include "espgoodies/debug.h"
#include "espgoodies/drivers/uart.h"
#include "espgoodies/flashcfg.h"
#include "espgoodies/initdata.h"
#include "espgoodies/ota.h"
#include "espgoodies/rtc.h"
#include "espgoodies/sleep.h"
#include "espgoodies/system.h"
#include "espgoodies/tcpserver.h"
#include "espgoodies/utils.h"
#include "espgoodies/wifi.h"

#include "client.h"
#include "common.h"
#include "config.h"
#include "core.h"
#include "device.h"
#include "ver.h"


#define CONNECT_TIMEOUT_CORE_POLLING 10  /* Seconds */
#define CONNECT_TIMEOUT_SETUP_MODE   300 /* Seconds */
#define SETUP_MODE_IDLE_TIMEOUT      300 /* Seconds */

#define WIFI_AUTO_SCAN_INTERVAL    60  /* Seconds */
#define WIFI_MIN_RSSI_THRESHOLD    -80
#define WIFI_BETTER_RSSI_THRESHOLD 15
#define WIFI_BETTER_COUNT          2

#ifndef FW_BASE_URL
#define FW_BASE_URL           ""
#define FW_BASE_OTA_PATH      ""
#endif
#define FW_LATEST_FILE        "/latest"
#define FW_LATEST_STABLE_FILE "/latest_stable"
#define FW_LATEST_BETA_FILE   "/latest_beta"
#define FW_AUTO_MIN_INTERVAL  24 /* Hours */


static bool       wifi_first_time_connected = FALSE;
static os_timer_t wifi_connect_timeout_core_polling_timer;
static os_timer_t wifi_connect_timeout_setup_mode_timer;
#ifdef _OTA
static os_timer_t ota_auto_timer;
static uint32     ota_auto_counter = 1;
#endif
static os_timer_t setup_mode_idle_timer;


static void ICACHE_FLASH_ATTR check_update_fw_config(void);
static void ICACHE_FLASH_ATTR main_init(void);

void        ICACHE_FLASH_ATTR user_init(void);
void        ICACHE_FLASH_ATTR user_rf_pre_init(void);
int         ICACHE_FLASH_ATTR user_rf_cal_sector_set(void);

static void ICACHE_FLASH_ATTR on_system_ready(void);
static void ICACHE_FLASH_ATTR on_system_reset(void);
static void ICACHE_FLASH_ATTR on_system_sleep(uint32 interval);

static void ICACHE_FLASH_ATTR on_setup_mode(bool active);
static void ICACHE_FLASH_ATTR on_setup_mode_idle_timeout(void *);

#ifdef _OTA
static void ICACHE_FLASH_ATTR on_ota_auto_timer(void *arg);
static void ICACHE_FLASH_ATTR on_ota_auto_perform(int code);
#endif

static void ICACHE_FLASH_ATTR on_wifi_connect(bool connected);
static void ICACHE_FLASH_ATTR on_wifi_connect_timeout_core_polling(void *arg);
static void ICACHE_FLASH_ATTR on_wifi_connect_timeout_setup_mode(void *arg);


void check_update_fw_config(void) {
    version_t fw_version;
    system_get_fw_version(&fw_version);

    if ((fw_version.major > FW_VERSION_MAJOR) ||
        (fw_version.major == 0 &&
         fw_version.minor == 0 &&
         fw_version.patch == 0 &&
         fw_version.label == 0 &&
         fw_version.type == 0)) {

        DEBUG_CONFIG("invalid firmware version detected, resetting configuration");
        flashcfg_reset(FLASH_CONFIG_SLOT_DEFAULT);
    }

    if (fw_version.major != FW_VERSION_MAJOR ||
        fw_version.minor != FW_VERSION_MINOR ||
        fw_version.patch != FW_VERSION_PATCH ||
        fw_version.label != FW_VERSION_LABEL ||
        fw_version.type != FW_VERSION_TYPE) {

        DEBUG_CONFIG("firmware update detected");
        fw_version.major = FW_VERSION_MAJOR;
        fw_version.minor = FW_VERSION_MINOR;
        fw_version.patch = FW_VERSION_PATCH;
        fw_version.label = FW_VERSION_LABEL;
        fw_version.type = FW_VERSION_TYPE;

        system_set_fw_version(&fw_version);
        system_config_save();
    }
}

void main_init(void) {
    system_init_done_cb(on_system_ready);

    /* Core polling Wi-Fi timeout */
    os_timer_disarm(&wifi_connect_timeout_core_polling_timer);
    os_timer_setfn(&wifi_connect_timeout_core_polling_timer, on_wifi_connect_timeout_core_polling, NULL);
    os_timer_arm(&wifi_connect_timeout_core_polling_timer, CONNECT_TIMEOUT_CORE_POLLING * 1000, /* repeat = */ FALSE);

    /* Setup mode Wi-Fi timeout */
    os_timer_disarm(&wifi_connect_timeout_setup_mode_timer);
    os_timer_setfn(&wifi_connect_timeout_setup_mode_timer, on_wifi_connect_timeout_setup_mode, NULL);
    os_timer_arm(&wifi_connect_timeout_setup_mode_timer, CONNECT_TIMEOUT_SETUP_MODE * 1000, /* repeat = */ FALSE);

    if (wifi_get_ssid()) {
        wifi_station_enable(
            device_name,
            on_wifi_connect,
            WIFI_AUTO_SCAN_INTERVAL,
            WIFI_MIN_RSSI_THRESHOLD,
            WIFI_BETTER_RSSI_THRESHOLD,
            WIFI_BETTER_COUNT
        );
    }
}

void on_system_ready(void) {
    DEBUG_SYSTEM("system initialization done");

    /* Generate a device-update event, mainly to be used with webhooks */
    event_push_device_update();

    if (!wifi_get_ssid()) {
        /* If no SSID set (no Wi-Fi network configured), enter setup mode */
        DEBUG_SYSTEM("no SSID configured");
        if (!system_setup_mode_active()) {
            system_setup_mode_toggle();
        }
    }
}

void on_system_reset(void) {
    DEBUG_SYSTEM("cleaning up before reset");

    core_disable_polling();
    config_ensure_saved();
    sessions_respond_all();
    tcp_server_stop();
}

void on_system_sleep(uint32 interval) {
    DEBUG_SYSTEM("cleaning up before sleep");

    core_disable_polling();
    config_ensure_saved();
    sessions_respond_all();
    tcp_server_stop();
}

void on_setup_mode(bool active) {
    os_timer_disarm(&setup_mode_idle_timer);

    if (active) {
        os_timer_setfn(&setup_mode_idle_timer, on_setup_mode_idle_timeout, NULL);
        os_timer_arm(&setup_mode_idle_timer, SETUP_MODE_IDLE_TIMEOUT * 1000, /* repeat = */ TRUE);

        if (strlen(DEFAULT_SSID)) {
            DEBUG_SYSTEM("enabling connection to default SSID");
            wifi_station_disable();
            wifi_station_temporary_enable(
                DEFAULT_SSID,
                /* psk = */ NULL,
                /* bssid = */ NULL,
                /* hostname = */ device_name,
                /* callback = */ NULL
            );
        }
    }
}

void on_setup_mode_idle_timeout(void *arg) {
    if (!system_setup_mode_active()) {
        return;
    }

    if (wifi_station_is_connected()) {
        return; /* Don't reset if connected to AP */
    }

    if (system_setup_mode_has_ap_clients()) {
        return; /* Don't reset if AP has clients */
    }

    DEBUG_SYSTEM("setup mode idle timeout");
    system_setup_mode_toggle();
}

#ifdef _OTA

void on_ota_auto_timer(void *arg) {
    if (!(device_flags & DEVICE_FLAG_OTA_AUTO_UPDATE)) {
        return;
    }

    if (system_uptime() > ota_auto_counter * FW_AUTO_MIN_INTERVAL * 3600) {
        ota_auto_counter++;
        ota_auto_update_check(/* beta = */ device_flags & DEVICE_FLAG_OTA_BETA_ENABLED, on_ota_auto_perform);
    }
}

void on_ota_auto_perform(int code) {
    config_ensure_saved();
    sessions_respond_all();
}

#endif /* _OTA */

void on_wifi_connect(bool connected) {
    if (!connected) {
        /* Re-arm the setup mode Wi-Fi timer upon disconnection */
        os_timer_disarm(&wifi_connect_timeout_setup_mode_timer);
        os_timer_setfn(&wifi_connect_timeout_setup_mode_timer, on_wifi_connect_timeout_setup_mode, NULL);
        os_timer_arm(&wifi_connect_timeout_setup_mode_timer, CONNECT_TIMEOUT_SETUP_MODE * 1000, /* repeat = */ FALSE);
        return;
    }

    DEBUG_SYSTEM("we're connected!");

    os_timer_disarm(&wifi_connect_timeout_setup_mode_timer);

    if (!wifi_first_time_connected) {
        wifi_first_time_connected = TRUE;
        os_timer_disarm(&wifi_connect_timeout_core_polling_timer);
        core_enable_polling();

        /* Attempt to fetch the latest provisioning configuration at each boot, right after Wi-Fi connection */
        config_start_auto_provisioning(/* ignore_version = */ FALSE);

#ifdef _SLEEP
        /* Start sleep timer now that we're connected */
        sleep_reset();
#endif

#ifdef _OTA
        /* Start firmware auto update mechanism */

        if (device_flags & DEVICE_FLAG_OTA_AUTO_UPDATE) {
#ifdef _SLEEP
            int wake_interval = sleep_get_wake_interval();
            if (wake_interval && sleep_get_wake_duration()) {
                /* Do not check for an update more often than each OTA_AUTO_MIN_INTERVAL hours */
                int sleep_boot_count = rtc_get_boot_count();
                int count_modulo = FW_AUTO_MIN_INTERVAL * 60 / wake_interval;
                if (count_modulo < 1) {  /* For sleep intervals longer than 24h */
                    count_modulo = 1;
                }

                DEBUG_SYSTEM(
                    "sleep boot count: %d %% %d = %d",
                    sleep_boot_count, count_modulo,
                    sleep_boot_count % count_modulo
                );

                if (sleep_boot_count % count_modulo == 0) {
                    ota_auto_update_check(
                        /* beta = */ device_flags & DEVICE_FLAG_OTA_BETA_ENABLED,
                        on_ota_auto_perform
                    );
                }
            }
            else {  /* Sleep disabled, check for update at each boot */
                ota_auto_update_check(/* beta = */ device_flags & DEVICE_FLAG_OTA_BETA_ENABLED, on_ota_auto_perform);
            }
#else  /* !_SLEEP */
            ota_auto_update_check(/* beta = */ device_flags & DEVICE_FLAG_OTA_BETA_ENABLED, on_ota_auto_perform);
#endif
        }
#endif  /* _OTA */
    }
}

void on_wifi_connect_timeout_core_polling(void *arg) {
    DEBUG_SYSTEM("initial Wi-Fi connection timeout, starting core polling");
    core_enable_polling();
#ifdef _SLEEP
    sleep_reset();
#endif
}

void on_wifi_connect_timeout_setup_mode(void *arg) {
    if (!system_setup_mode_active()) {
        DEBUG_SYSTEM("Wi-Fi connection timeout, switching to setup mode");
        system_setup_mode_toggle();
    }
}


/* Main functions */

void user_rf_pre_init(void) {
    init_data_ensure();

    system_deep_sleep_set_option(2);  /* No RF calibration after waking from deep sleep */
    system_phy_set_rfoption(2);       /* No RF calibration after waking from deep sleep */
    system_phy_set_powerup_option(2); /* Calibration only for VDD33 and Tx power */
}

int user_rf_cal_sector_set(void) {
    /* Always consider a 1MB flash size; set RF_CAL to the 5th last sector */
    return 256 - 5;
}

void user_init(void) {
    system_timer_reinit();
#ifdef _DEBUG
    debug_uart_setup(_DEBUG_UART_NO);
    os_delay_us(10000);

    printf("\n\n");
    DEBUG_SYSTEM("espQToggle  " FW_VERSION);
    DEBUG_SYSTEM("API Version " API_VERSION);
    DEBUG_SYSTEM("SDK Version " ESP_SDK_VERSION_STRING);
    DEBUG_SYSTEM("Chip ID     %08X", system_get_chip_id());
    DEBUG_SYSTEM("Flash ID    %08X", spi_flash_get_id());

#else /* !_DEBUG */
    debug_uart_setup(DEBUG_UART_DISABLE);
#endif /* _DEBUG */

    system_reset_set_callbacks(on_system_reset, config_factory_reset);
    system_setup_mode_set_callback(on_setup_mode);

    rtc_init();
    system_check_reboot_loop();
#ifdef _SLEEP
    sleep_init(on_system_sleep);
#endif
    system_config_load();
    check_update_fw_config();
    config_init();
    system_init();
#ifdef _OTA
    ota_init(
        /* current_version = */   FW_VERSION,
        /* latest_url = */        FW_BASE_URL FW_BASE_OTA_PATH FW_LATEST_FILE,
        /* latest_stable_url = */ FW_BASE_URL FW_BASE_OTA_PATH FW_LATEST_STABLE_FILE,
        /* latest_beta_url = */   FW_BASE_URL FW_BASE_OTA_PATH FW_LATEST_BETA_FILE,
        /* url_template = */      FW_BASE_URL FW_BASE_OTA_PATH "/%s"
    );

    os_timer_disarm(&ota_auto_timer);
    os_timer_setfn(&ota_auto_timer, on_ota_auto_timer, NULL);
    os_timer_arm(&ota_auto_timer, 60 * 1000, /* repeat = */ TRUE);
#endif
    wifi_init();
    client_init();
    core_init();
    main_init();
}
