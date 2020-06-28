
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
#include "espgoodies/flashcfg.h"
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


#define CONNECT_TIMEOUT             20000   /* Milliseconds */
#define DEBUG_BAUD                  115200

#define FW_LATEST_FILE              "/latest"
#define FW_LATEST_STABLE_FILE       "/latest_stable"
#define FW_LATEST_BETA_FILE         "/latest_beta"
#define FW_AUTO_MIN_INTERVAL        24  /* Hours */

#define RTC_UNEXP_RESET_COUNT_ADDR  RTC_USER_ADDR + 0  /* 130 * 4 bytes = 520 */
#define MAX_UNEXP_RESET_COUNT       16


static bool                         wifi_first_time_connected = FALSE;
static os_timer_t                   connect_timeout_timer;
#ifdef _OTA
static os_timer_t                   ota_auto_timer;
static uint32                       ota_auto_counter = 1;
#endif


ICACHE_FLASH_ATTR static void       check_update_fw_config(void);
ICACHE_FLASH_ATTR static void       check_reboot_loop(void);
ICACHE_FLASH_ATTR static void       main_init(void);

#ifdef _DEBUG
ICACHE_FLASH_ATTR static void       debug_putc_func(char c);
#endif
ICACHE_FLASH_ATTR void              user_init(void);
ICACHE_FLASH_ATTR void              user_rf_pre_init(void);
ICACHE_FLASH_ATTR int               user_rf_cal_sector_set(void);

ICACHE_FLASH_ATTR static void       on_system_ready(void);
ICACHE_FLASH_ATTR static void       on_system_reset(void);

ICACHE_FLASH_ATTR static void       on_setup_mode(bool active);

#ifdef _OTA
ICACHE_FLASH_ATTR static void       on_ota_auto_timer(void *arg);
ICACHE_FLASH_ATTR static void       on_ota_auto_perform(int code);
#endif

ICACHE_FLASH_ATTR static void       on_wifi_connect(bool connected);
ICACHE_FLASH_ATTR static void       on_wifi_connect_timeout(void *arg);


void check_update_fw_config(void) {
    uint8 major, minor, patch, label, type;
    system_get_fw_version(&major, &minor, &patch, &label, &type);
    if ((major > FW_VERSION_MAJOR) || (major == 0 && minor == 0 && patch == 0 && label == 0 && type == 0)) {
        DEBUG_CONFIG("invalid firmware version detected, resetting configuration");
        flashcfg_reset(FLASH_CONFIG_SLOT_DEFAULT);
    }

    if (major != FW_VERSION_MAJOR ||
        minor != FW_VERSION_MINOR ||
        patch != FW_VERSION_PATCH ||
        label != FW_VERSION_LABEL ||
        type != FW_VERSION_TYPE) {

        DEBUG_CONFIG("firmware update detected");
        major = FW_VERSION_MAJOR;
        minor = FW_VERSION_MINOR;
        patch = FW_VERSION_PATCH;
        label = FW_VERSION_LABEL;
        type = FW_VERSION_TYPE;

        system_set_fw_version(major, minor, patch, label, type);
        system_config_save();
    }
}

void check_reboot_loop(void) {
    struct rst_info *reset_info = system_get_rst_info();
    uint32 unexp_reset_count;

    if (reset_info->reason == REASON_WDT_RST || reset_info->reason == REASON_EXCEPTION_RST) {
        DEBUG_SYSTEM("unexpected reset detected");
        unexp_reset_count = rtc_get_value(RTC_UNEXP_RESET_COUNT_ADDR) + 1;

        if (unexp_reset_count == MAX_UNEXP_RESET_COUNT) {
            DEBUG_SYSTEM("too many unexpected resets, resetting configuration");
            flashcfg_reset(FLASH_CONFIG_SLOT_DEFAULT);
        }
    }

    else {
        unexp_reset_count = 0;
    }

    rtc_set_value(RTC_UNEXP_RESET_COUNT_ADDR, unexp_reset_count);
}

void main_init(void) {
    system_init_done_cb(on_system_ready);

    os_timer_disarm(&connect_timeout_timer);
    os_timer_setfn(&connect_timeout_timer, on_wifi_connect_timeout, NULL);
    os_timer_arm(&connect_timeout_timer, CONNECT_TIMEOUT, /* repeat = */ FALSE);

    if (wifi_get_ssid()) {
        wifi_station_enable(device_name, on_wifi_connect);
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

    config_ensure_saved();
    sessions_respond_all();
    tcp_server_stop();
}

void on_setup_mode(bool active) {
    if (active && strlen(DEFAULT_SSID)) {
        DEBUG_SYSTEM("enabling connection to default SSID");
        wifi_station_disable();
        wifi_station_temporary_enable(DEFAULT_SSID, /* psk = */ NULL, /* bssid = */ NULL,
                                      /* hostname = */ device_name, /* callback = */ NULL);
    }
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
    if (wifi_first_time_connected) {
        return;  /* We're only interested in first time connection */
    }

    if (!connected) {
        return;  /* We don't care about disconnections */
    }

    DEBUG_SYSTEM("we're connected!");

    wifi_first_time_connected = TRUE;
    os_timer_disarm(&connect_timeout_timer);
    core_enable_polling();

    if (!(device_flags & DEVICE_FLAG_CONFIGURED)) {
        DEBUG_SYSTEM("system not configured, starting provisioning");
        config_start_provisioning();
    }
    else {
        DEBUG_SYSTEM("system configured");
    }

#ifdef _SLEEP
    /* Start sleep timer now that we're connected */
    sleep_reset();
#endif

#ifdef _OTA
    /* Start firmware auto update mechanism */

    if (device_flags & DEVICE_FLAG_OTA_AUTO_UPDATE) {
#ifdef _SLEEP
        int wake_interval = sleep_get_wake_interval();
        if (wake_interval) {
            /* Do not check for an update more often than each OTA_AUTO_MIN_INTERVAL hours */
            int sleep_boot_count = rtc_get_boot_count();
            int count_modulo = FW_AUTO_MIN_INTERVAL * 60 / wake_interval;
            if (count_modulo < 1) {  /* For sleep intervals longer than 24h */
                count_modulo = 1;
            }

            DEBUG_SYSTEM("sleep boot count: %d %% %d = %d", sleep_boot_count, count_modulo,
                         sleep_boot_count % count_modulo);

            if (sleep_boot_count % count_modulo == 0) {
                ota_auto_update_check(/* beta = */ device_flags & DEVICE_FLAG_OTA_BETA_ENABLED, on_ota_auto_perform);
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

void on_wifi_connect_timeout(void *arg) {
    DEBUG_SYSTEM("timeout waiting for initial Wi-Fi connection");
    core_enable_polling();
#ifdef _SLEEP
    sleep_reset();
#endif
}


    /* Main functions */

void user_rf_pre_init(void) {
    init_data_ensure();

    system_deep_sleep_set_option(2);    /* No RF calibration after waking from deep sleep */
    system_phy_set_rfoption(2);         /* No RF calibration after waking from deep sleep */
    system_phy_set_powerup_option(2);   /* Calibration only for VDD33 and Tx power */
}

int user_rf_cal_sector_set(void) {
    /* Always consider a 1MB flash size; set RF_CAL to the 5th last sector */
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
DEBUG("API Version " API_VERSION);
DEBUG("SDK Version " ESP_SDK_VERSION_STRING);

#else /* !_DEBUG */
    system_set_os_print(0);
#endif /* _DEBUG */

    system_reset_set_callback(on_system_reset);
    system_setup_mode_set_callback(on_setup_mode);

    rtc_init();
    check_reboot_loop();
#ifdef _SLEEP
    sleep_init();
#endif
    system_config_init();
    check_update_fw_config();
    config_init();
#ifdef _OTA
    ota_init(/* current_version = */   FW_VERSION,
             /* latest_url = */        FW_BASE_URL FW_BASE_OTA_PATH "/" FW_LATEST_FILE,
             /* latest_stable_url = */ FW_BASE_URL FW_BASE_OTA_PATH "/" FW_LATEST_STABLE_FILE,
             /* latest_beta_url = */   FW_BASE_URL FW_BASE_OTA_PATH "/" FW_LATEST_BETA_FILE,
             /* url_template = */      FW_BASE_URL FW_BASE_OTA_PATH "/%s");

    os_timer_disarm(&ota_auto_timer);
    os_timer_setfn(&ota_auto_timer, on_ota_auto_timer, NULL);
    os_timer_arm(&ota_auto_timer, 60 * 1000, /* repeat = */ TRUE);
#endif
    wifi_init();
    client_init();
    core_init();
    main_init();
}
