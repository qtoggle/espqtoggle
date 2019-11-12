
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

#include <mem.h>

#include "espgoodies/common.h"
#include "espgoodies/flashcfg.h"
#include "espgoodies/wifi.h"
#include "espgoodies/pingwdt.h"
#include "espgoodies/crypto.h"

#ifdef _SLEEP
#include "espgoodies/sleep.h"
#endif

#include "api.h"
#include "apiutils.h"
#include "webhooks.h"
#include "device.h"
#include "config.h"
#include "stringpool.h"


#define NULL_HASH                       "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"

ICACHE_FLASH_ATTR void                  device_load(uint8 *data);
ICACHE_FLASH_ATTR void                  device_save(uint8 *data, uint32 *strings_offs);


void device_load(uint8 *data) {
    int frequency;
    int wifi_scan_interval = 0, wifi_scan_threshold = 0;
    char *strings_ptr = (char *) data + CONFIG_OFFS_STR_BASE;
    char wifi_ssid[WIFI_SSID_MAX_LEN];
    uint8 wifi_bssid[WIFI_BSSID_LEN];
    char wifi_psk[WIFI_PSK_MAX_LEN];
    ip_addr_t wifi_ip, wifi_gw, wifi_dns;
    int wifi_netmask;
    uint16 ping_wdt_interval;

#ifdef _SLEEP
    uint16 wake_interval, wake_duration;
#endif

    /* device name */
    memcpy(device_hostname, data + CONFIG_OFFS_HOSTNAME, API_MAX_DEVICE_NAME_LEN);
    memcpy(device_display_name, data + CONFIG_OFFS_DISP_NAME, API_MAX_DEVICE_DISP_NAME_LEN);

    /* passwords */
    memcpy(device_admin_password_hash, data + CONFIG_OFFS_ADMIN_PASSWORD, SHA256_LEN);
    memcpy(device_normal_password_hash, data + CONFIG_OFFS_NORMAL_PASSWORD, SHA256_LEN);
    memcpy(device_viewonly_password_hash, data + CONFIG_OFFS_VIEWONLY_PASSWORD, SHA256_LEN);

    /* transform default (null) password hashes into hashes of empty strings */
    char *hex_digest;
    if (!memcmp(device_admin_password_hash, NULL_HASH, SHA256_LEN)) {
        memcpy(device_admin_password_hash, EMPTY_SHA256_HEX, SHA256_HEX_LEN);
        DEBUG_DEVICE("empty admin password");
    }
    else {
        /* passwords are stored as binary digest, must be converted to hex digest before use */
        hex_digest = bin2hex((uint8 *) device_admin_password_hash, SHA256_LEN);
        strncpy(device_admin_password_hash, hex_digest, SHA256_HEX_LEN + 1);
        free(hex_digest);
    }

    if (!memcmp(device_normal_password_hash, NULL_HASH, SHA256_LEN)) {
        memcpy(device_normal_password_hash, EMPTY_SHA256_HEX, SHA256_HEX_LEN);
        DEBUG_DEVICE("empty normal password");
    }
    else {
        /* passwords are stored as binary digest, must be converted to hex digest before use */
        hex_digest = bin2hex((uint8 *) device_normal_password_hash, SHA256_LEN);
        strncpy(device_normal_password_hash, hex_digest, SHA256_HEX_LEN + 1);
        free(hex_digest);
    }

    if (!memcmp(device_viewonly_password_hash, NULL_HASH, SHA256_LEN)) {
        memcpy(device_viewonly_password_hash, EMPTY_SHA256_HEX, SHA256_HEX_LEN);
        DEBUG_DEVICE("empty viewonly password");
    }
    else {
        /* passwords are stored as binary digest, must be converted to hex digest before use */
        hex_digest = bin2hex((uint8 *) device_viewonly_password_hash, SHA256_LEN);
        strncpy(device_viewonly_password_hash, hex_digest, SHA256_HEX_LEN + 1);
        free(hex_digest);
    }

    /* wifi */
    memcpy(wifi_ssid, data + CONFIG_OFFS_SSID, WIFI_SSID_MAX_LEN);
    memcpy(wifi_bssid, data + CONFIG_OFFS_BSSID, WIFI_BSSID_LEN);
    memcpy(wifi_psk, data + CONFIG_OFFS_PSK, WIFI_PSK_MAX_LEN);
    memcpy(&wifi_scan_interval, data + CONFIG_OFFS_SCAN_INTERVAL, 2);
    memcpy(&wifi_scan_threshold, data + CONFIG_OFFS_SCAN_THRESH, 1);

    memcpy(&wifi_ip.addr, data + CONFIG_OFFS_IP, 4);
    memcpy(&wifi_gw.addr, data + CONFIG_OFFS_GW, 4);
    memcpy(&wifi_dns.addr, data + CONFIG_OFFS_DNS, 4);
    memcpy(&wifi_netmask, data + CONFIG_OFFS_NETMASK, 1);

    if (!wifi_scan_threshold) {
        wifi_scan_threshold = WIFI_SCAN_THRESH_DEF;
    }

    if (wifi_ssid[0] || wifi_bssid[0]) {
        wifi_set_scan_threshold(wifi_scan_threshold);
        wifi_set_scan_interval(wifi_scan_interval);
        wifi_set_ssid_psk(wifi_ssid, wifi_bssid, wifi_psk);
        wifi_set_ip(&wifi_ip, wifi_netmask, &wifi_gw, &wifi_dns);
    }

    /* flags & others */
    memcpy(&device_tcp_port, data + CONFIG_OFFS_TCP_PORT, 2);
    memcpy(&ping_wdt_interval, data + CONFIG_OFFS_PING_INTERVAL, 2);
    memcpy(&device_flags, data + CONFIG_OFFS_DEVICE_FLAGS, 4);
    memcpy(&frequency, data + CONFIG_OFFS_CPU_FREQ, 4);
    if (frequency) {
        system_update_cpu_freq(frequency);
    }

    /* config model */
    char *model = string_pool_read(strings_ptr, data + CONFIG_OFFS_MODEL);
    if (model) {
        strncpy(device_config_model, model, API_MAX_DEVICE_CONFIG_MODEL_LEN);
    }

    /* webhooks */
    if ((webhooks_host = string_pool_read_dup(strings_ptr, data + CONFIG_OFFS_WEBHOOKS_HOST))) {
        DEBUG_WEBHOOKS("webhooks host = \"%s\"", webhooks_host);
    }
    else {
        DEBUG_WEBHOOKS("null webhooks host");
    }

    memcpy(&webhooks_port, data + CONFIG_OFFS_WEBHOOKS_PORT, 2);
    DEBUG_WEBHOOKS("webhooks port = %d", webhooks_port);

    if ((webhooks_path = string_pool_read_dup(strings_ptr, data + CONFIG_OFFS_WEBHOOKS_PATH))) {
        DEBUG_WEBHOOKS("webhooks path = \"%s\"", webhooks_path);
    }
    else {
        DEBUG_WEBHOOKS("null webhooks path");
    }

    char *password_hash = string_pool_read(strings_ptr, data + CONFIG_OFFS_WEBHOOKS_PASSWORD);
    if (password_hash) {
        strncpy(webhooks_password_hash, password_hash, SHA256_HEX_LEN + 1);
    }
    if (!webhooks_password_hash[0]) {  /* use hash of empty string, by default */
        DEBUG_WEBHOOKS("null webhooks password");

        char *hex_digest = sha256_hex("");
        strncpy(webhooks_password_hash, hex_digest, SHA256_HEX_LEN + 1);
        free(hex_digest);
    }

    memcpy(&webhooks_events_mask, data + CONFIG_OFFS_WEBHOOKS_EVENTS, 2);
    DEBUG_WEBHOOKS("webhooks events mask = %02X", webhooks_events_mask);

    memcpy(&webhooks_timeout, data + CONFIG_OFFS_WEBHOOKS_TIMEOUT, 2);
    webhooks_retries = data[CONFIG_OFFS_WEBHOOKS_RETRIES];
    DEBUG_WEBHOOKS("webhooks retries = %d", webhooks_retries);

    if (!webhooks_timeout) {
        webhooks_timeout = WEBHOOKS_DEF_TIMEOUT;
    }
    DEBUG_WEBHOOKS("webhooks timeout = %d", webhooks_timeout);

    /* ping watchdog */
    if (ping_wdt_interval) {
        ping_wdt_start(ping_wdt_interval);
    }

#ifdef _SLEEP
    /* sleep mode */
    memcpy(&wake_interval, data + CONFIG_OFFS_WAKE_INTERVAL, 2);
    memcpy(&wake_duration, data + CONFIG_OFFS_WAKE_DURATION, 2);

    sleep_set_wake_interval(wake_interval);
    sleep_set_wake_duration(wake_duration);
#endif
}

void device_save(uint8 *data, uint32 *strings_offs) {
    int frequency = system_get_cpu_freq();
    int wifi_scan_interval = wifi_get_scan_interval();
    char wifi_scan_threshold = wifi_get_scan_threshold();
    char *strings_ptr = (char *) data + CONFIG_OFFS_STR_BASE;
    ip_addr_t *wifi_ip, *wifi_gw, *wifi_dns;
    int wifi_netmask;
    uint32 zero = 0;
    uint16 ping_wdt_interval = ping_wdt_get_interval();

#ifdef _SLEEP
    uint16 wake_interval = sleep_get_wake_interval();
    uint16 wake_duration = sleep_get_wake_duration();
#endif

    /* device name */
    memcpy(data + CONFIG_OFFS_HOSTNAME, device_hostname, API_MAX_DEVICE_NAME_LEN);
    memcpy(data + CONFIG_OFFS_DISP_NAME, device_display_name, API_MAX_DEVICE_DISP_NAME_LEN);

    /* passwords - stored as binary digests */
    uint8 *digest = hex2bin(device_admin_password_hash);
    memcpy(data + CONFIG_OFFS_ADMIN_PASSWORD, digest, SHA256_LEN);
    free(digest);

    digest = hex2bin(device_normal_password_hash);
    memcpy(data + CONFIG_OFFS_NORMAL_PASSWORD, digest, SHA256_LEN);
    free(digest);

    digest = hex2bin(device_viewonly_password_hash);
    memcpy(data + CONFIG_OFFS_VIEWONLY_PASSWORD, digest, SHA256_LEN);
    free(digest);

    /* wifi */
    memcpy(data + CONFIG_OFFS_SSID, wifi_get_ssid(), WIFI_SSID_MAX_LEN);
    memcpy(data + CONFIG_OFFS_BSSID, wifi_get_bssid(), WIFI_BSSID_LEN);
    strncpy((char *) data + CONFIG_OFFS_PSK, wifi_get_psk() ? wifi_get_psk() : "", WIFI_PSK_MAX_LEN);
    memcpy(data + CONFIG_OFFS_SCAN_INTERVAL, &wifi_scan_interval, 2);
    memcpy(data + CONFIG_OFFS_SCAN_THRESH, &wifi_scan_threshold, 1);

    wifi_ip = wifi_get_static_ip();
    wifi_netmask = wifi_get_static_netmask();
    wifi_gw = wifi_get_static_gw();
    wifi_dns = wifi_get_static_dns();
    memcpy(data + CONFIG_OFFS_IP, wifi_ip ? &wifi_ip->addr : &zero, 4);
    memcpy(data + CONFIG_OFFS_GW, wifi_gw ? &wifi_gw->addr : &zero, 4);
    memcpy(data + CONFIG_OFFS_DNS, wifi_dns ? &wifi_dns->addr : &zero, 4);
    memcpy(data + CONFIG_OFFS_NETMASK, &wifi_netmask, 1);

    /* flags & others */
    memcpy(data + CONFIG_OFFS_TCP_PORT, &device_tcp_port, 2);
    memcpy(data + CONFIG_OFFS_PING_INTERVAL, &ping_wdt_interval, 2);
    memcpy(data + CONFIG_OFFS_DEVICE_FLAGS, &device_flags, 4);
    memcpy(data + CONFIG_OFFS_CPU_FREQ, &frequency, 4);

    /* config model */
    if (!string_pool_write(strings_ptr, strings_offs, device_config_model, data + CONFIG_OFFS_MODEL)) {
        DEBUG_DEVICE("no more string space to save config model");
    }

    /* webhooks */
    if (!string_pool_write(strings_ptr, strings_offs, webhooks_host, data + CONFIG_OFFS_WEBHOOKS_HOST)) {
        DEBUG_WEBHOOKS("no more string space to save host");
    }

    memcpy(data + CONFIG_OFFS_WEBHOOKS_PORT, &webhooks_port, 2);

    if (!string_pool_write(strings_ptr, strings_offs, webhooks_path, data + CONFIG_OFFS_WEBHOOKS_PATH)) {
        DEBUG_WEBHOOKS("no more string space to save path");
    }

    if (!string_pool_write(strings_ptr, strings_offs, webhooks_password_hash, data + CONFIG_OFFS_WEBHOOKS_PASSWORD)) {
        DEBUG_WEBHOOKS("no more string space to save password");
    }

    memcpy(data + CONFIG_OFFS_WEBHOOKS_EVENTS, &webhooks_events_mask, 2);
    memcpy(data + CONFIG_OFFS_WEBHOOKS_TIMEOUT, &webhooks_timeout, 2);
    data[CONFIG_OFFS_WEBHOOKS_RETRIES] = webhooks_retries;

#ifdef _SLEEP
    /* sleep mode */
    memcpy(data + CONFIG_OFFS_WAKE_INTERVAL, &wake_interval, 2);
    memcpy(data + CONFIG_OFFS_WAKE_DURATION, &wake_duration, 2);
#endif
}


void config_init(void) {
    uint8 *config_data = zalloc(FLASH_CONFIG_SIZE);
    flashcfg_load(config_data);

    device_load(config_data);

    DEBUG_DEVICE("CPU frequency set to %d MHz", system_get_cpu_freq());

    if (!device_hostname[0]) {
        snprintf(device_hostname, API_MAX_DEVICE_NAME_LEN, DEFAULT_HOSTNAME, system_get_chip_id());
    }

    DEBUG_DEVICE("hostname is \"%s\"", device_hostname);

    if (!device_tcp_port) {
        device_tcp_port = DEFAULT_TCP_PORT;
    }

    if (!device_config_model[0] && device_config_model_choices[0]) {
        strncpy(device_config_model, device_config_model_choices[0], API_MAX_DEVICE_CONFIG_MODEL_LEN);
    }

    DEBUG_DEVICE("config model is \"%s\"", device_config_model);

    ports_init(config_data);

    /* at this point we no longer need the config data */
    free(config_data);

    /* parse port expressions */
    port_t *p, **port = all_ports;
    while ((p = *port++)) {
        if (p->sexpr) {
            p->expr = expr_parse(p->id, p->sexpr, strlen(p->sexpr));
            if (p->expr) {
                DEBUG_PORT(p, "value expression successfully parsed");
            }
            else {
                DEBUG_PORT(p, "value expression parse failed");
                free(p->sexpr);
                p->sexpr = NULL;
            }
        }

        if (p->stransform_write) {
            p->transform_write = expr_parse(p->id, p->stransform_write, strlen(p->stransform_write));
            if (p->transform_write) {
                DEBUG_PORT(p, "write transform successfully parsed");
            }
            else {
                DEBUG_PORT(p, "write transform parse failed");
                free(p->stransform_write);
                p->stransform_write = NULL;
            }
        }

        if (p->stransform_read) {
            p->transform_read = expr_parse(p->id, p->stransform_read, strlen(p->stransform_read));
            if (p->transform_read) {
                DEBUG_PORT(p, "read transform successfully parsed");
            }
            else {
                DEBUG_PORT(p, "read transform parse failed");
                free(p->stransform_read);
                p->stransform_read = NULL;
            }
        }

        port_rebuild_change_dep_mask(p);
    }
}

void config_save(void) {
    uint8 *config_data = zalloc(FLASH_CONFIG_SIZE);
    uint32 strings_offs = 1; /* address 0 in strings pool represents an unset string, so it's left out */

    flashcfg_load(config_data);
    ports_save(config_data, &strings_offs);
    device_save(config_data, &strings_offs);
    flashcfg_save(config_data);
    free(config_data);

    DEBUG_FLASHCFG("total strings size is %d", strings_offs - 1);
}
