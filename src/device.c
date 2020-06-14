
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

#include "espgoodies/crypto.h"
#include "espgoodies/sleep.h"
#include "espgoodies/wifi.h"

#include "api.h"
#include "apiutils.h"
#include "common.h"
#include "config.h"
#include "stringpool.h"
#include "webhooks.h"
#include "device.h"


#define NULL_HASH "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"


char   *device_name = NULL;
char   *device_display_name = NULL;
char    device_admin_password_hash[SHA256_HEX_LEN + 1] = {0};
char    device_normal_password_hash[SHA256_HEX_LEN + 1] = {0};
char    device_viewonly_password_hash[SHA256_HEX_LEN + 1] = {0};
char    device_config_name[API_MAX_DEVICE_CONFIG_NAME_LEN + 1] = {0};   // TODO: this could be allocated dynamically
uint32  device_flags = 0;
uint16  device_tcp_port = 0;
uint16  device_provisioning_version = 0;


void device_load(uint8 *config_data) {
    char *strings_ptr = (char *) config_data + CONFIG_OFFS_STR_BASE;
    ip_addr_t wifi_ip, wifi_gw, wifi_dns;
    uint8 wifi_netmask;

#ifdef _SLEEP
    uint16 wake_interval, wake_duration;
#endif

    /* Device name */
    device_name = string_pool_read_dup(strings_ptr, config_data + CONFIG_OFFS_DEVICE_NAME);
    if (!device_name) {
        char default_device_name[API_MAX_DEVICE_NAME_LEN];
        snprintf(default_device_name, sizeof(default_device_name), DEFAULT_HOSTNAME, system_get_chip_id());
        device_name = strdup(default_device_name);
    }
    DEBUG_DEVICE("device name = \"%s\"", device_name);

    /* Device display_name */
    device_display_name = string_pool_read_dup(strings_ptr, config_data + CONFIG_OFFS_DEVICE_DISP_NAME);
    if (!device_display_name) {
        device_display_name = strdup("");
    }
    DEBUG_DEVICE("device display_name = \"%s\"", device_display_name);

    /* Passwords */
    memcpy(device_admin_password_hash, config_data + CONFIG_OFFS_ADMIN_PASSWORD, SHA256_LEN);
    memcpy(device_normal_password_hash, config_data + CONFIG_OFFS_NORMAL_PASSWORD, SHA256_LEN);
    memcpy(device_viewonly_password_hash, config_data + CONFIG_OFFS_VIEWONLY_PASSWORD, SHA256_LEN);

    /* Transform default (null) password hashes into hashes of empty strings */
    char *hex_digest;
    if (!memcmp(device_admin_password_hash, NULL_HASH, SHA256_LEN)) {
        memcpy(device_admin_password_hash, EMPTY_SHA256_HEX, SHA256_HEX_LEN);
        DEBUG_DEVICE("empty admin password");
    }
    else {
        /* Passwords are stored as binary digest, must be converted to hex digest before use */
        hex_digest = bin2hex((uint8 *) device_admin_password_hash, SHA256_LEN);
        strncpy(device_admin_password_hash, hex_digest, SHA256_HEX_LEN + 1);
        free(hex_digest);
    }

    if (!memcmp(device_normal_password_hash, NULL_HASH, SHA256_LEN)) {
        memcpy(device_normal_password_hash, EMPTY_SHA256_HEX, SHA256_HEX_LEN);
        DEBUG_DEVICE("empty normal password");
    }
    else {
        /* Passwords are stored as binary digest, must be converted to hex digest before use */
        hex_digest = bin2hex((uint8 *) device_normal_password_hash, SHA256_LEN);
        strncpy(device_normal_password_hash, hex_digest, SHA256_HEX_LEN + 1);
        free(hex_digest);
    }

    if (!memcmp(device_viewonly_password_hash, NULL_HASH, SHA256_LEN)) {
        memcpy(device_viewonly_password_hash, EMPTY_SHA256_HEX, SHA256_HEX_LEN);
        DEBUG_DEVICE("empty viewonly password");
    }
    else {
        /* Passwords are stored as binary digest, must be converted to hex digest before use */
        hex_digest = bin2hex((uint8 *) device_viewonly_password_hash, SHA256_LEN);
        strncpy(device_viewonly_password_hash, hex_digest, SHA256_HEX_LEN + 1);
        free(hex_digest);
    }

    /* IP configuration */
    memcpy(&wifi_ip.addr, config_data + CONFIG_OFFS_IP_ADDRESS, 4);
    memcpy(&wifi_gw.addr, config_data + CONFIG_OFFS_GATEWAY, 4);
    memcpy(&wifi_dns.addr, config_data + CONFIG_OFFS_DNS, 4);
    memcpy(&wifi_netmask, config_data + CONFIG_OFFS_NETMASK, 1);

    wifi_set_ip_address(wifi_ip);
    wifi_set_netmask(wifi_netmask);
    wifi_set_gateway(wifi_gw);
    wifi_set_dns(wifi_dns);

    /* Flags & others */
    memcpy(&device_tcp_port, config_data + CONFIG_OFFS_TCP_PORT, 2);
    memcpy(&device_flags, config_data + CONFIG_OFFS_DEVICE_FLAGS, 4);
    DEBUG_DEVICE("flags = %08X", device_flags);

    /* Provisioning version */
    memcpy(&device_provisioning_version, config_data + CONFIG_OFFS_PROVISIONING_VER, 2);
    DEBUG_CONFIG("provisioning version = %d", device_provisioning_version);

    /* Configuration name */
    char *config_name = string_pool_read(strings_ptr, config_data + CONFIG_OFFS_CONFIG_NAME);
    if (config_name) {
        strncpy(device_config_name, config_name, API_MAX_DEVICE_CONFIG_NAME_LEN);
        device_config_name[API_MAX_DEVICE_CONFIG_NAME_LEN - 1] = 0;
    }

    DEBUG_CONFIG("config name = \"%s\"", device_config_name);

    /* Webhooks */
    if ((webhooks_host = string_pool_read_dup(strings_ptr, config_data + CONFIG_OFFS_WEBHOOKS_HOST))) {
        DEBUG_WEBHOOKS("webhooks host = \"%s\"", webhooks_host);
    }
    else {
        DEBUG_WEBHOOKS("webhooks host = \"\"");
    }

    memcpy(&webhooks_port, config_data + CONFIG_OFFS_WEBHOOKS_PORT, 2);
    DEBUG_WEBHOOKS("webhooks port = %d", webhooks_port);

    if ((webhooks_path = string_pool_read_dup(strings_ptr, config_data + CONFIG_OFFS_WEBHOOKS_PATH))) {
        DEBUG_WEBHOOKS("webhooks path = \"%s\"", webhooks_path);
    }
    else {
        DEBUG_WEBHOOKS("webhooks path = \"\"");
    }

    char *password_hash = string_pool_read(strings_ptr, config_data + CONFIG_OFFS_WEBHOOKS_PASSWORD);
    if (password_hash) {
        strncpy(webhooks_password_hash, password_hash, SHA256_HEX_LEN + 1);
    }
    if (!webhooks_password_hash[0]) {  /* Use hash of empty string, by default */
        DEBUG_WEBHOOKS("webhooks password = \"\"");

        char *hex_digest = sha256_hex("");
        strncpy(webhooks_password_hash, hex_digest, SHA256_HEX_LEN + 1);
        free(hex_digest);
    }

    memcpy(&webhooks_events_mask, config_data + CONFIG_OFFS_WEBHOOKS_EVENTS, 2);
    DEBUG_WEBHOOKS("webhooks events mask = %02X", webhooks_events_mask);

    memcpy(&webhooks_timeout, config_data + CONFIG_OFFS_WEBHOOKS_TIMEOUT, 2);
    webhooks_retries = config_data[CONFIG_OFFS_WEBHOOKS_RETRIES];
    DEBUG_WEBHOOKS("webhooks retries = %d", webhooks_retries);

    if (!webhooks_timeout) {
        webhooks_timeout = WEBHOOKS_DEF_TIMEOUT;
    }
    DEBUG_WEBHOOKS("webhooks timeout = %d", webhooks_timeout);

#ifdef _SLEEP
    /* Sleep mode */
    memcpy(&wake_interval, config_data + CONFIG_OFFS_WAKE_INTERVAL, 2);
    memcpy(&wake_duration, config_data + CONFIG_OFFS_WAKE_DURATION, 2);

    sleep_set_wake_interval(wake_interval);
    sleep_set_wake_duration(wake_duration);
#endif
}

void device_save(uint8 *config_data, uint32 *strings_offs) {
    char *strings_ptr = (char *) config_data + CONFIG_OFFS_STR_BASE;
    ip_addr_t wifi_ip_address, wifi_gateway, wifi_dns;
    uint8 wifi_netmask;

#ifdef _SLEEP
    uint16 wake_interval = sleep_get_wake_interval();
    uint16 wake_duration = sleep_get_wake_duration();
#endif

    /* Device name */
    if (!string_pool_write(strings_ptr, strings_offs, device_name, config_data + CONFIG_OFFS_DEVICE_NAME)) {
        DEBUG_DEVICE("no more strings pool space");
    }

    /* Device display name */
    if (!string_pool_write(strings_ptr, strings_offs, device_display_name, config_data + CONFIG_OFFS_DEVICE_DISP_NAME)) {
        DEBUG_DEVICE("no more strings pool space");
    }

    /* Passwords - stored as binary digests */
    uint8 *digest = hex2bin(device_admin_password_hash);
    memcpy(config_data + CONFIG_OFFS_ADMIN_PASSWORD, digest, SHA256_LEN);
    free(digest);

    digest = hex2bin(device_normal_password_hash);
    memcpy(config_data + CONFIG_OFFS_NORMAL_PASSWORD, digest, SHA256_LEN);
    free(digest);

    digest = hex2bin(device_viewonly_password_hash);
    memcpy(config_data + CONFIG_OFFS_VIEWONLY_PASSWORD, digest, SHA256_LEN);
    free(digest);

    /* IP configuration */
    wifi_ip_address = wifi_get_ip_address();
    wifi_netmask = wifi_get_netmask();
    wifi_gateway = wifi_get_gateway();
    wifi_dns = wifi_get_dns();
    memcpy(config_data + CONFIG_OFFS_IP_ADDRESS, &wifi_ip_address, 4);
    memcpy(config_data + CONFIG_OFFS_GATEWAY, &wifi_gateway, 4);
    memcpy(config_data + CONFIG_OFFS_DNS, &wifi_dns, 4);
    memcpy(config_data + CONFIG_OFFS_NETMASK, &wifi_netmask, 1);

    /* Flags & others */
    memcpy(config_data + CONFIG_OFFS_TCP_PORT, &device_tcp_port, 2);
    memcpy(config_data + CONFIG_OFFS_DEVICE_FLAGS, &device_flags, 4);

    /* Provisioning version */
    memcpy(config_data + CONFIG_OFFS_PROVISIONING_VER, &device_provisioning_version, 2);

    /* Configuration name */
    if (!string_pool_write(strings_ptr, strings_offs, device_config_name, config_data + CONFIG_OFFS_CONFIG_NAME)) {
        DEBUG_DEVICE("no more strings pool space");
    }

    /* Webhooks */
    if (!string_pool_write(strings_ptr, strings_offs, webhooks_host, config_data + CONFIG_OFFS_WEBHOOKS_HOST)) {
        DEBUG_WEBHOOKS("no more strings pool space");
    }

    memcpy(config_data + CONFIG_OFFS_WEBHOOKS_PORT, &webhooks_port, 2);

    if (!string_pool_write(strings_ptr, strings_offs, webhooks_path, config_data + CONFIG_OFFS_WEBHOOKS_PATH)) {
        DEBUG_WEBHOOKS("no more strings pool space");
    }

    if (!string_pool_write(strings_ptr, strings_offs, webhooks_password_hash, config_data + CONFIG_OFFS_WEBHOOKS_PASSWORD)) {
        DEBUG_WEBHOOKS("no more strings pool space");
    }

    memcpy(config_data + CONFIG_OFFS_WEBHOOKS_EVENTS, &webhooks_events_mask, 2);
    memcpy(config_data + CONFIG_OFFS_WEBHOOKS_TIMEOUT, &webhooks_timeout, 2);
    config_data[CONFIG_OFFS_WEBHOOKS_RETRIES] = webhooks_retries;

#ifdef _SLEEP
    /* Sleep mode */
    memcpy(config_data + CONFIG_OFFS_WAKE_INTERVAL, &wake_interval, 2);
    memcpy(config_data + CONFIG_OFFS_WAKE_DURATION, &wake_duration, 2);
#endif
}
