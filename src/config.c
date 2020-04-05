
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
#include "espgoodies/crypto.h"
#include "espgoodies/flashcfg.h"
#include "espgoodies/httpclient.h"
#include "espgoodies/json.h"
#include "espgoodies/system.h"
#include "espgoodies/wifi.h"

#ifdef _SLEEP
#include "espgoodies/sleep.h"
#endif

#include "api.h"
#include "apiutils.h"
#include "common.h"
#include "device.h"
#include "ports.h"
#include "stringpool.h"
#include "webhooks.h"
#include "config.h"


#define NULL_HASH                       "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"


static bool                             provisioning = FALSE;


ICACHE_FLASH_ATTR void                  device_load(uint8 *data);
ICACHE_FLASH_ATTR void                  device_save(uint8 *data, uint32 *strings_offs);

ICACHE_FLASH_ATTR void                  on_config_provisioning_response(char *body, int body_len, int status,
                                                                        char *header_names[], char *header_values[],
                                                                        int header_count, uint8 addr[]);
ICACHE_FLASH_ATTR void                  apply_device_provisioning_config(json_t *device_config);
ICACHE_FLASH_ATTR void                  apply_ports_provisioning_config(json_t *ports_config);
ICACHE_FLASH_ATTR void                  apply_port_provisioning_config(json_t *port_config);


void device_load(uint8 *data) {
    int frequency;
    char *strings_ptr = (char *) data + CONFIG_OFFS_STR_BASE;
    char wifi_ssid[WIFI_SSID_MAX_LEN];
    uint8 wifi_bssid[WIFI_BSSID_LEN];
    char wifi_psk[WIFI_PSK_MAX_LEN];
    ip_addr_t wifi_ip, wifi_gw, wifi_dns;
    uint8 wifi_netmask;

#ifdef _SLEEP
    uint16 wake_interval, wake_duration;
#endif

    /* device name */
    memcpy(device_hostname, data + CONFIG_OFFS_HOSTNAME, API_MAX_DEVICE_NAME_LEN);
    DEBUG_DEVICE("device hostname = \"%s\"", device_hostname);

    /* device display name */
    memcpy(device_display_name, data + CONFIG_OFFS_DISP_NAME, API_MAX_DEVICE_DISP_NAME_LEN);
    DEBUG_DEVICE("device display_name = \"%s\"", device_display_name);

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

    memcpy(&wifi_ip.addr, data + CONFIG_OFFS_IP_ADDRESS, 4);
    memcpy(&wifi_gw.addr, data + CONFIG_OFFS_GATEWAY, 4);
    memcpy(&wifi_dns.addr, data + CONFIG_OFFS_DNS, 4);
    memcpy(&wifi_netmask, data + CONFIG_OFFS_NETMASK, 1);

    if (wifi_ssid[0] || wifi_bssid[0]) {
        wifi_set_ssid(wifi_ssid);
        wifi_set_psk(wifi_psk);
        wifi_set_bssid(wifi_bssid);

        wifi_set_ip_address(wifi_ip);
        wifi_set_netmask(wifi_netmask);
        wifi_set_gateway(wifi_gw);
        wifi_set_dns(wifi_dns);
    }

    /* flags & others */
    memcpy(&device_tcp_port, data + CONFIG_OFFS_TCP_PORT, 2);
    memcpy(&device_flags, data + CONFIG_OFFS_DEVICE_FLAGS, 4);
    DEBUG_DEVICE("flags = %08X", device_flags);

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
        DEBUG_WEBHOOKS("webhooks host = \"\"");
    }

    memcpy(&webhooks_port, data + CONFIG_OFFS_WEBHOOKS_PORT, 2);
    DEBUG_WEBHOOKS("webhooks port = %d", webhooks_port);

    if ((webhooks_path = string_pool_read_dup(strings_ptr, data + CONFIG_OFFS_WEBHOOKS_PATH))) {
        DEBUG_WEBHOOKS("webhooks path = \"%s\"", webhooks_path);
    }
    else {
        DEBUG_WEBHOOKS("webhooks path = \"\"");
    }

    char *password_hash = string_pool_read(strings_ptr, data + CONFIG_OFFS_WEBHOOKS_PASSWORD);
    if (password_hash) {
        strncpy(webhooks_password_hash, password_hash, SHA256_HEX_LEN + 1);
    }
    if (!webhooks_password_hash[0]) {  /* use hash of empty string, by default */
        DEBUG_WEBHOOKS("webhooks password = \"\"");

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
    char *strings_ptr = (char *) data + CONFIG_OFFS_STR_BASE;
    ip_addr_t wifi_ip_address, wifi_gateway, wifi_dns;
    uint8 wifi_netmask;

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

    wifi_ip_address = wifi_get_ip_address();
    wifi_netmask = wifi_get_netmask();
    wifi_gateway = wifi_get_gateway();
    wifi_dns = wifi_get_dns();
    memcpy(data + CONFIG_OFFS_IP_ADDRESS, &wifi_ip_address, 4);
    memcpy(data + CONFIG_OFFS_GATEWAY, &wifi_gateway, 4);
    memcpy(data + CONFIG_OFFS_DNS, &wifi_dns, 4);
    memcpy(data + CONFIG_OFFS_NETMASK, &wifi_netmask, 1);

    /* flags & others */
    memcpy(data + CONFIG_OFFS_TCP_PORT, &device_tcp_port, 2);
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

    /* parse port value expressions */
    port_t *p, **port = all_ports;
    while ((p = *port++)) {
        if (!IS_ENABLED(p)) {
            continue;
        }

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

void config_start_provisioning(void) {
    if (provisioning) {
        DEBUG_DEVICE("provisioning: busy");
        return;
    }

    provisioning = TRUE;

    char url[256];
    if (device_config_model_choices[0]) {
        snprintf(url, sizeof(url), "%s%s/%s/%s.json",
                 FW_BASE_URL, FW_BASE_CFG_PATH, FW_CONFIG_NAME, device_config_model);
    }
    else {
        snprintf(url, sizeof(url), "%s%s/%s.json", FW_BASE_URL, FW_BASE_CFG_PATH, FW_CONFIG_NAME);
    }

    DEBUG_DEVICE("provisioning: fetching from %s", url);

    httpclient_request("GET", url, /* body = */ NULL, /* body_len = */ 0,
                       /* header_names = */ NULL, /* header_values = */ NULL, /* header_count = */ 0,
                       on_config_provisioning_response, HTTP_DEF_TIMEOUT);
}

void on_config_provisioning_response(char *body, int body_len, int status, char *header_names[], char *header_values[],
                                     int header_count, uint8 addr[]) {

    provisioning = FALSE;

    if (status == 200) {
        json_t *config = json_parse(body);
        if (!config) {
            DEBUG_DEVICE("provisioning: invalid json");
            return;
        }

        if (json_get_type(config) == JSON_TYPE_OBJ) {
            DEBUG_DEVICE("provisioning: got config");

            /* temporarily set API access level to admin */
            api_conn_save();
            api_conn_set((void *) 1, API_ACCESS_LEVEL_ADMIN);

            json_t *device_config = json_obj_lookup_key(config, "device");
            apply_device_provisioning_config(device_config);

            json_t *ports_config = json_obj_lookup_key(config, "ports");
            apply_ports_provisioning_config(ports_config);

            api_conn_restore();

            json_free(config);
        }
        else {
            DEBUG_DEVICE("provisioning: invalid config");
        }
    }
    else {
        DEBUG_DEVICE("provisioning: got status %d", status);
    }
}

void apply_device_provisioning_config(json_t *device_config) {
    json_t *response_json;
    json_t *attr_json;
    int code;

    DEBUG_DEVICE("provisioning: applying device config");

    /* never update device name */
    attr_json = json_obj_pop_key(device_config, "name");
    if (attr_json) {
        json_free(attr_json);
    }

    /* don't overwrite display name */
    if (device_display_name[0]) {
        attr_json = json_obj_pop_key(device_config, "display_name");
        if (attr_json) {
            json_free(attr_json);
        }
    }

    if (device_config && json_get_type(device_config) == JSON_TYPE_OBJ) {
        DEBUG_DEVICE("provisioning: setting device attributes");
        code = 200;
        response_json = api_patch_device(/* query_json = */ NULL, device_config, &code);
        json_free(response_json);
        if (code / 100 != 2) {
            DEBUG_DEVICE("provisioning: api_patch_device() failed with status code %d", code);
        }
    }
    else {
        DEBUG_DEVICE("provisioning: invalid or missing device config");
    }
}

void apply_ports_provisioning_config(json_t *ports_config) {
    json_t *port_config;
    int i;

    DEBUG_DEVICE("provisioning: applying ports config");

    if (ports_config && json_get_type(ports_config) == JSON_TYPE_LIST) {
        for (i = 0; i < json_list_get_len(ports_config); i++) {
            port_config = json_list_value_at(ports_config, i);
            apply_port_provisioning_config(port_config);
        }
    }
    else {
        DEBUG_DEVICE("provisioning: invalid or missing ports config");
    }
}

void apply_port_provisioning_config(json_t *port_config) {
    json_t *response_json;
    json_t *port_id_json;
    json_t *virtual_json;
    json_t *request_json;
    json_t *attr_json;
    json_t *value_json;
    port_t *port;
    int code;

    if (port_config && json_get_type(port_config) == JSON_TYPE_OBJ) {
        port_id_json = json_obj_pop_key(port_config, "id");
        virtual_json = json_obj_pop_key(port_config, "virtual");
        if (port_id_json && json_get_type(port_id_json) == JSON_TYPE_STR) {
            char *port_id = json_str_get(port_id_json);
            DEBUG_DEVICE("provisioning: applying port %s config", port_id);
            port = port_find_by_id(port_id);

            /* virtual ports have to be added first */
            if (virtual_json && json_get_type(virtual_json) == JSON_TYPE_BOOL && json_bool_get(virtual_json)) {
                if (port) {
                    DEBUG_DEVICE("provisioning: virtual port %s exists, removing it first", port_id);
                    code = 200;
                    response_json = api_delete_port(port, /* query_json = */ NULL, &code);
                    json_free(response_json);
                    if (code / 100 != 2) {
                        DEBUG_DEVICE("provisioning: api_delete_port() failed with status code %d", code);
                    }
                }

                DEBUG_DEVICE("provisioning: adding virtual port %s", port_id);
                request_json = json_obj_new();
                json_obj_append(request_json, "id", json_str_new(port_id));

                /* pass non-modifiable virtual port attributes from port_config to request_json */
                attr_json = json_obj_pop_key(port_config, "type");
                if (attr_json) {
                    json_obj_append(request_json, "type", attr_json);
                }
                attr_json = json_obj_pop_key(port_config, "min");
                if (attr_json) {
                    json_obj_append(request_json, "min", attr_json);
                }
                attr_json = json_obj_pop_key(port_config, "max");
                if (attr_json) {
                    json_obj_append(request_json, "max", attr_json);
                }
                attr_json = json_obj_pop_key(port_config, "integer");
                if (attr_json) {
                    json_obj_append(request_json, "integer", attr_json);
                }
                attr_json = json_obj_pop_key(port_config, "step");
                if (attr_json) {
                    json_obj_append(request_json, "step", attr_json);
                }
                attr_json = json_obj_pop_key(port_config, "choices");
                if (attr_json) {
                    json_obj_append(request_json, "choices", attr_json);
                }

                code = 200;
                response_json = api_post_ports(/* query_json = */ NULL, request_json, &code);
                json_free(response_json);
                if (code / 100 != 2) {
                    DEBUG_DEVICE("provisioning: api_post_ports() failed with status code %d", code);
                }
                json_free(request_json);

                /* lookup port reference after adding it */
                port = port_find_by_id(port_id);
            }

            value_json = json_obj_pop_key(port_config, "value");

            DEBUG_DEVICE("provisioning: setting port %s attributes", port_id);

            code = 200;
            response_json = api_patch_port(port, /* query_json = */ NULL, port_config, &code);
            json_free(response_json);
            if (code / 100 != 2) {
                DEBUG_DEVICE("provisioning: api_patch_port() failed with status code %d", code);
            }

            if (value_json) { /* if value was also supplied with provisioning */
                DEBUG_DEVICE("provisioning: setting port %s value", port_id);

                code = 200;
                response_json = api_patch_port_value(port, /* query_json = */ NULL, value_json, &code);
                json_free(response_json);
                if (code / 100 != 2) {
                    DEBUG_DEVICE("provisioning: api_patch_port_value() failed with status code %d", code);
                }
                json_free(value_json);
            }
        }
        else {
            DEBUG_DEVICE("provisioning: invalid or missing port id");
        }

        if (port_id_json) {
            json_free(port_id_json);
        }
        if (virtual_json) {
            json_free(virtual_json);
        }
    }
    else {
        DEBUG_DEVICE("provisioning: invalid or missing port config");
    }
}
