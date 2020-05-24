
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
#include <ctype.h>

#include <mem.h>
#include <user_interface.h>

#include "espgoodies/common.h"
#include "espgoodies/crypto.h"
#include "espgoodies/flashcfg.h"
#include "espgoodies/gpio.h"
#include "espgoodies/httpserver.h"
#include "espgoodies/system.h"
#include "espgoodies/utils.h"
#include "espgoodies/wifi.h"

#ifdef _BATTERY
#include "espgoodies/battery.h"
#endif

#ifdef _SLEEP
#include "espgoodies/sleep.h"
#endif

#ifdef _OTA
#include "espgoodies/ota.h"
#endif

#include "drivers/hspi.h"
#include "client.h"
#include "common.h"
#include "config.h"
#include "core.h"
#include "device.h"
#include "jsonrefs.h"
#include "ports.h"
#include "sessions.h"
#include "ver.h"
#include "webhooks.h"
#include "api.h"
#include "apiutils.h"


#define _API_ERROR(c, error, field_name, field_value) ({                                                          \
    if (response_json) json_free(response_json);                                        \
    json_obj_append(response_json = json_obj_new(), "error", json_str_new(error));      \
    if (field_name) {                                                                   \
        json_obj_append(response_json, field_name, json_str_new(field_value));          \
    }                                                                                   \
    *code = c;                                                                          \
    response_json;                                                                      \
})

#define API_ERROR(c, error) _API_ERROR(c, error, NULL, NULL)

#define FORBIDDEN(level) ({                                                             \
    if (response_json) json_free(response_json);                                        \
    json_obj_append(response_json = json_obj_new(), "error",                            \
            json_str_new("forbidden"));                                                 \
                                                                                        \
    switch (level) {                                                                    \
        case API_ACCESS_LEVEL_ADMIN:                                                    \
            json_obj_append(response_json, "required_level", json_str_new("admin"));    \
            break;                                                                      \
                                                                                        \
        case API_ACCESS_LEVEL_NORMAL:                                                   \
            json_obj_append(response_json, "required_level", json_str_new("normal"));   \
            break;                                                                      \
                                                                                        \
        case API_ACCESS_LEVEL_VIEWONLY:                                                 \
            json_obj_append(response_json, "required_level", json_str_new("viewonly")); \
            break;                                                                      \
                                                                                        \
        default:                                                                        \
            json_obj_append(response_json, "required_level", json_str_new("none"));     \
            break;                                                                      \
    }                                                                                   \
                                                                                        \
    *code = 403;                                                                        \
    response_json;                                                                      \
})

#define INVALID_EXPRESSION(reason, token, pos) ({                                       \
    if (response_json) json_free(response_json);                                        \
    response_json = json_obj_new();                                                     \
    json_obj_append(response_json, "error", json_str_new("invalid-field"));             \
    json_obj_append(response_json, "field", json_str_new("expression"));                \
    json_t *details_json = json_obj_new();                                              \
    json_obj_append(details_json, "reason", json_str_new(reason));                      \
    if (token) {                                                                        \
        json_obj_append(details_json, "token", json_str_new(token));                    \
    }                                                                                   \
    if (pos >= 1) {                                                                     \
        json_obj_append(details_json, "pos", json_int_new(pos));                        \
    }                                                                                   \
    json_obj_append(response_json, "details", details_json);                            \
    *code = 400;                                                                        \
    response_json;                                                                      \
})

#define INVALID_EXPRESSION_FROM_ERROR() ({                                              \
    expr_parse_error_t *parse_error = expr_parse_get_error();                           \
    INVALID_EXPRESSION(parse_error->reason,                                             \
                       parse_error->token,                                              \
                       parse_error->pos);                                               \
})

#define MISSING_FIELD(field) _API_ERROR(400, "missing-field", "field", field)
#define INVALID_FIELD(field) _API_ERROR(400, "invalid-field", "field", field)

#define SEQ_INVALID_FIELD(field) ({                                                     \
    free(port->sequence_values);                                                        \
    free(port->sequence_delays);                                                        \
    port->sequence_pos = -1;                                                            \
    port->sequence_repeat = -1;                                                         \
    INVALID_FIELD(field);                                                               \
})

#define ATTR_NOT_MODIFIABLE(attr) _API_ERROR(400, "attribute-not-modifiable", "attribute", attr)

#define NO_SUCH_ATTRIBUTE(attr) _API_ERROR(400, "no-such-attribute", "attribute", attr)

#define RESPOND_NO_SUCH_FUNCTION() {                                                    \
    API_ERROR(404, "no-such-function");                                                 \
    goto response;                                                                      \
}

#define WIFI_RSSI_EXCELLENT     -55
#define WIFI_RSSI_GOOD          -65
#define WIFI_RSSI_FAIR          -75


static uint8                    api_access_level;
static uint8                    api_access_level_saved;
static struct espconn         * api_conn;
static struct espconn         * api_conn_saved;


static char                   * frequency_choices[] = {"80", "160", NULL};

#ifdef _OTA
static char                   * ota_states_str[] = {"idle", "checking", "downloading", "restarting"};
#endif


ICACHE_FLASH_ATTR static json_t       * port_attrdefs_to_json(port_t *port, json_refs_ctx_t *json_refs_ctx);
ICACHE_FLASH_ATTR static json_t       * device_attrdefs_to_json(void);

ICACHE_FLASH_ATTR static void           on_sequence_timer(void *arg);

#ifdef _OTA
ICACHE_FLASH_ATTR static void           on_ota_latest(char *version, char *date, char *url);
ICACHE_FLASH_ATTR static void           on_ota_perform(int code);
#endif

ICACHE_FLASH_ATTR static void           on_wifi_scan(wifi_scan_result_t *results, int len);


json_t *api_call_handle(int method, char* path, json_t *query_json, json_t *request_json, int *code) {
    char *part1 = NULL, *part2 = NULL, *part3 = NULL;
    char *token;
    *code = 200;

    json_t *response_json = NULL;
    port_t *port;

    if (method == HTTP_METHOD_OTHER) {
        RESPOND_NO_SUCH_FUNCTION();
    }

    /* Remove the leading & trailing slashes */

    while (path[0] == '/') {
        path++;
    }

    int path_len = strlen(path);
    while (path[path_len - 1] == '/') {
        path[path_len - 1] = 0;
        path_len--;
    }

    /* Split path */

    token = strtok(path, "/");
    if (!token) {
        RESPOND_NO_SUCH_FUNCTION();
    }
    part1 = strdup(token);

    token = strtok(NULL, "/");
    if (token) {
        part2 = strdup(token);
    }

    token = strtok(NULL, "/");
    if (token) {
        part3 = strdup(token);
    }

    token = strtok(NULL, "/");
    if (token) {
        RESPOND_NO_SUCH_FUNCTION();
    }


    /* Determine the api call */

    if (!strcmp(part1, "device")) {
        if (part2) {
            RESPOND_NO_SUCH_FUNCTION();
        }
        else {
            if (method == HTTP_METHOD_GET) {
                response_json = api_get_device(query_json, code);
            }
            else if (method == HTTP_METHOD_PATCH) {
                response_json = api_patch_device(query_json, request_json, code);
            }
            else {
                RESPOND_NO_SUCH_FUNCTION();
            }
        }
    }
    else if (!strcmp(part1, "reset") && method == HTTP_METHOD_POST && !part2) {
        response_json = api_post_reset(query_json, request_json, code);
    }
#ifdef _OTA
    else if (!strcmp(part1, "firmware") && !part2) {
        if (method == HTTP_METHOD_GET) {
            response_json = api_get_firmware(query_json, code);
        }
        else if (method == HTTP_METHOD_PATCH) {
            response_json = api_patch_firmware(query_json, request_json, code);
        }
        else {
            RESPOND_NO_SUCH_FUNCTION();
        }
    }
#endif
    else if (!strcmp(part1, "access")) {
        if (!part2) {
            response_json = api_get_access(query_json, code);
        }
        else {
            RESPOND_NO_SUCH_FUNCTION();
        }
    }
    else if (!strcmp(part1, "ports")) {
        if (part2) {
            port = port_find_by_id(part2);
            if (!port) {
                API_ERROR(404, "no-such-port");
                goto response;
            }

            if (part3) {
                if (!strcmp(part3, "value")) { /* /ports/{id}/value */
                    if (method == HTTP_METHOD_GET) {
                        response_json = api_get_port_value(port, query_json, code);
                    }
                    else if (method == HTTP_METHOD_PATCH) {
                        response_json = api_patch_port_value(port, query_json, request_json, code);
                    }
                    else {
                        RESPOND_NO_SUCH_FUNCTION();
                    }
                }
                else if (!strcmp(part3, "sequence")) { /* /ports/{id}/sequence */
                    if (method == HTTP_METHOD_PATCH) {
                        response_json = api_patch_port_sequence(port, query_json, request_json, code);
                    }
                    else {
                        RESPOND_NO_SUCH_FUNCTION();
                    }
                }
                else {
                    RESPOND_NO_SUCH_FUNCTION();
                }
            }
            else { /* /ports/{id} */
                if (method == HTTP_METHOD_PATCH) {
                    response_json = api_patch_port(port, query_json, request_json, code);
                }
#ifdef HAS_VIRTUAL
                else if (method == HTTP_METHOD_DELETE) {
                    response_json = api_delete_port(port, query_json, code);
                }
#endif
                else {
                    RESPOND_NO_SUCH_FUNCTION();
                }
            }
        }
        else { /* /ports */
            if (method == HTTP_METHOD_GET) {
                response_json = api_get_ports(query_json, code);
            }
#ifdef HAS_VIRTUAL
            else if (method == HTTP_METHOD_POST) {
                response_json = api_post_ports( query_json, request_json, code);
            }
#endif
            else {
                RESPOND_NO_SUCH_FUNCTION();
            }
        }
    }
    else if (!strcmp(part1, "webhooks")) {
        if (part2) {
            RESPOND_NO_SUCH_FUNCTION();
        }

        if (method == HTTP_METHOD_GET) {
            response_json = api_get_webhooks(query_json, code);
        }
        else if (method == HTTP_METHOD_PATCH) {
            response_json = api_patch_webhooks(query_json, request_json, code);
        }
        else {
            RESPOND_NO_SUCH_FUNCTION();
        }
    }
    else if (!strcmp(part1, "wifi")) {
        if (part2) {
            RESPOND_NO_SUCH_FUNCTION();
        }

        if (method == HTTP_METHOD_GET) {
            response_json = api_get_wifi(query_json, code);
        }
        else {
            RESPOND_NO_SUCH_FUNCTION();
        }
    }
    else if (!strcmp(part1, "rawio")) {
        if (part2) {
            if (part3) {
                RESPOND_NO_SUCH_FUNCTION();
            }

            if (method == HTTP_METHOD_GET) {
                response_json = api_get_raw_io(part2, query_json, code);
            }
            else if (method == HTTP_METHOD_PATCH) {
                response_json = api_patch_raw_io(part2, query_json, request_json, code);
            }
            else {
                RESPOND_NO_SUCH_FUNCTION();
            }
        }
        else {
            RESPOND_NO_SUCH_FUNCTION();
        }
    }
    else {
        RESPOND_NO_SUCH_FUNCTION();
    }

    response:

    if (part1) {
        free(part1);
    }
    if (part2) {
        free(part2);
    }
    if (part3) {
        free(part3);
    }

    return response_json;
}

void api_conn_set(struct espconn *conn, int access_level) {
    if (api_conn) {
        DEBUG_API("overwriting existing API connection");
    }

    api_conn = conn;
    api_access_level = access_level;
}

bool api_conn_busy(void) {
    return !!api_conn;
}

bool api_conn_equal(struct espconn *conn) {
    if (!api_conn) {
        return FALSE;
    }

    return CONN_EQUAL(conn, api_conn);
}

uint8 api_conn_access_level_get(void) {
    return api_access_level;
}

void api_conn_reset(void) {
    api_conn = NULL;
    api_access_level = API_ACCESS_LEVEL_NONE;
}

void api_conn_save(void) {
    DEBUG_API("saving connection state");

    api_conn_saved = api_conn;
    api_access_level_saved = api_access_level;

    api_conn = NULL;
    api_access_level = API_ACCESS_LEVEL_NONE;
}

void api_conn_restore(void) {
    DEBUG_API("restoring connection state");

    api_conn = api_conn_saved;
    api_access_level = api_access_level_saved;

    api_conn_saved = NULL;
    api_access_level_saved = API_ACCESS_LEVEL_NONE;
}


json_t *port_to_json(port_t *port, json_refs_ctx_t *json_refs_ctx) {
    json_t *json = json_obj_new();

    /* Common to all ports */

    json_obj_append(json, "id", json_str_new(port->id));
    json_obj_append(json, "display_name", json_str_new(port->display_name ? port->display_name : ""));
    json_obj_append(json, "writable", json_bool_new(IS_OUTPUT(port)));
    json_obj_append(json, "enabled", json_bool_new(IS_ENABLED(port)));
    json_obj_append(json, "persisted", json_bool_new(IS_PERSISTED(port)));
    json_obj_append(json, "internal", json_bool_new(IS_INTERNAL(port)));

    if (IS_VIRTUAL(port)) {
        json_obj_append(json, "virtual", json_bool_new(TRUE));
    }
    else {
        json_obj_append(json, "sampling_interval", json_int_new(port->sampling_interval));
    }

    if (port->stransform_read) {
        json_obj_append(json, "transform_read", json_str_new(port->stransform_read));
    }
    else {
        json_obj_append(json, "transform_read", json_str_new(""));
    }

    /* Specific to numeric ports */
    if (port->type == PORT_TYPE_NUMBER) {
        json_obj_append(json, "type", json_str_new(API_PORT_TYPE_NUMBER));
        json_obj_append(json, "unit", json_str_new(port->unit ? port->unit : ""));

        if (!IS_UNDEFINED(port->min)) {
            json_obj_append(json, "min", json_double_new(port->min));
        }
        if (!IS_UNDEFINED(port->max)) {
            json_obj_append(json, "max", json_double_new(port->max));
        }
        if (!IS_UNDEFINED(port->step)) {
            json_obj_append(json, "step", json_double_new(port->step));
        }
        if (port->integer) {
            json_obj_append(json, "integer", json_bool_new(TRUE));
        }

        if (port->choices) {
            int8 found_ref_index = -1;
            if (json_refs_ctx->type == JSON_REFS_TYPE_PORTS_LIST) {
                /* Look through previous ports for same choices */
                port_t *p, **ports = all_ports;
                uint8 i = 0;
                while ((p = *ports++) && (p != port)) {
                    if (choices_equal(port->choices, p->choices)) {
                        found_ref_index = i;
                        break;
                    }

                    i++;
                }
            }

            if (found_ref_index >= 0) {
                /* Found equal choices at found_ref_index */
                json_t *ref = make_json_ref("#/%d/choices", found_ref_index);
#if defined(_DEBUG) && defined(_DEBUG_API)
                char *ref_str = json_str_get(json_obj_value_at(ref, 0));
                DEBUG_API("replacing \"%s.choices\" with $ref \"%s\"", port->id, ref_str);
#endif
                json_obj_append(json, "choices", ref);

            }
            else {
                json_t *list = json_list_new();
                char *c, **choices = port->choices;
                while ((c = *choices++)) {
                    json_list_append(list, choice_to_json(c, PORT_TYPE_NUMBER));
                }

                json_obj_append(json, "choices", list);
            }
        }
    }
    /* Specific to boolean ports */
    else { /* Assuming PORT_TYPE_BOOLEAN */
        json_obj_append(json, "type", json_str_new(API_PORT_TYPE_BOOLEAN));
    }

    /* Specific to output ports */
    if (IS_OUTPUT(port)) {
        if (port->sexpr) {
            json_obj_append(json, "expression", json_str_new(port->sexpr));
        }
        else {
            json_obj_append(json, "expression", json_str_new(""));
        }

        if (port->stransform_write) {
            json_obj_append(json, "transform_write", json_str_new(port->stransform_write));
        }
        else {
            json_obj_append(json, "transform_write", json_str_new(""));
        }
    }

    /* Extra attributes */
    if (port->attrdefs) {
        attrdef_t *a, **attrdefs = port->attrdefs;
        int index;
        while ((a = *attrdefs++)) {
            switch (a->type) {
                case ATTR_TYPE_BOOLEAN:
                    json_obj_append(json, a->name, json_bool_new(((int_getter_t) a->get)(port, a)));
                    break;

                case ATTR_TYPE_NUMBER:
                    if (a->choices) {
                        /* When dealing with choices, the getter returns the index inside the choices array */
                        index = ((int_getter_t) a->get)(port, a);
                        json_obj_append(json, a->name, json_double_new(get_choice_value_num(a->choices[index])));
                    }
                    else {
                        if (a->integer) {
                            json_obj_append(json, a->name, json_int_new(((int_getter_t) a->get)(port, a)));
                        }
                        else { /* float */
                            json_obj_append(json, a->name, json_int_new(((float_getter_t) a->get)(port, a)));
                        }
                    }
                    break;

                case ATTR_TYPE_STRING:
                    if (a->choices) {
                        /* When dealing with choices, the getter returns the index inside the choices array */
                        index = ((int_getter_t) a->get)(port, a);
                        json_obj_append(json, a->name, json_str_new(get_choice_value_str(a->choices[index])));
                    }
                    else {
                        json_obj_append(json, a->name, json_str_new(((str_getter_t) a->get)(port, a)));
                    }
                    break;
            }
        }
    }

    /* Port value */
    json_obj_append(json, "value", port_get_json_value(port));

    /* Attribute definitions */
    json_obj_append(json, "definitions", port_attrdefs_to_json(port, json_refs_ctx));

    json_stringify(json);

    return json;
}

json_t *device_to_json(void) {
    char value[256];
    json_t *json = json_obj_new();

    /* Common attributes */
    json_obj_append(json, "name", json_str_new(device_name));
    json_obj_append(json, "display_name", json_str_new(device_display_name));
    json_obj_append(json, "version", json_str_new(FW_VERSION));
    json_obj_append(json, "api_version", json_str_new(API_VERSION));
    json_obj_append(json, "vendor", json_str_new(VENDOR));

    /* Passwords - never reveal them */
    json_obj_append(json, "admin_password", json_str_new(""));
    json_obj_append(json, "normal_password", json_str_new(""));
    json_obj_append(json, "viewonly_password", json_str_new(""));

    /* Flags */
    json_t *flags_json = json_list_new();

    json_list_append(flags_json, json_str_new("expressions"));
#ifdef _OTA
    json_list_append(flags_json, json_str_new("firmware"));
#endif
    json_list_append(flags_json, json_str_new("listen"));
    json_list_append(flags_json, json_str_new("sequences"));
#ifdef _SSL
    json_list_append(flags_json, json_str_new("ssl"));
#endif
    json_list_append(flags_json, json_str_new("webhooks"));

    json_obj_append(json, "flags", flags_json);

    /* Various optional attributes */
    json_obj_append(json, "uptime", json_int_new(system_uptime()));

#ifdef HAS_VIRTUAL
    json_obj_append(json, "virtual_ports", json_int_new(VIRTUAL_MAX_PORTS));
#endif

    /* IP configuration */
    ip_addr_t ip = wifi_get_ip_address();
    if (ip.addr) {
        snprintf(value, 256, IP_FMT, IP2STR(&ip));
        json_obj_append(json, "ip_address", json_str_new(value));
    }
    else {
        json_obj_append(json, "ip_address", json_str_new(""));
    }

    json_obj_append(json, "ip_netmask", json_int_new(wifi_get_netmask()));

    ip = wifi_get_gateway();
    if (ip.addr) {
        snprintf(value, 256, IP_FMT, IP2STR(&ip));
        json_obj_append(json, "ip_gateway", json_str_new(value));
    }
    else {
        json_obj_append(json, "ip_gateway", json_str_new(""));
    }

    ip = wifi_get_dns();
    if (ip.addr) {
        snprintf(value, 256, IP_FMT, IP2STR(&ip));
        json_obj_append(json, "ip_dns", json_str_new(value));
    }
    else {
        json_obj_append(json, "ip_dns", json_str_new(""));
    }

    /* Current IP info */
    ip = wifi_get_ip_address_current();
    if (ip.addr) {
        snprintf(value, 256, IP_FMT, IP2STR(&ip));
        json_obj_append(json, "ip_address_current", json_str_new(value));
    }
    else {
        json_obj_append(json, "ip_address_current", json_str_new(""));
    }

    json_obj_append(json, "ip_netmask_current", json_int_new(wifi_get_netmask_current()));

    ip = wifi_get_gateway_current();
    if (ip.addr) {
        snprintf(value, 256, IP_FMT, IP2STR(&ip));
        json_obj_append(json, "ip_gateway_current", json_str_new(value));
    }
    else {
        json_obj_append(json, "ip_gateway_current", json_str_new(""));
    }

    ip = wifi_get_dns_current();
    if (ip.addr) {
        snprintf(value, 256, IP_FMT, IP2STR(&ip));
        json_obj_append(json, "ip_dns_current", json_str_new(value));
    }
    else {
        json_obj_append(json, "ip_dns_current", json_str_new(""));
    }

    /* Wi-Fi configuration */
    char *ssid = wifi_get_ssid();
    char *psk = wifi_get_psk();
    uint8 *bssid = wifi_get_bssid();

    if (ssid) {
        json_obj_append(json, "wifi_ssid", json_str_new(ssid));
    }
    else {
        json_obj_append(json, "wifi_ssid", json_str_new(""));
    }

    if (psk) {
        json_obj_append(json, "wifi_key", json_str_new(psk));
    }
    else {
        json_obj_append(json, "wifi_key", json_str_new(""));
    }

    if (bssid) {
        snprintf(value, 256, "%02X:%02X:%02X:%02X:%02X:%02X", BSSID2STR(bssid));
        json_obj_append(json, "wifi_bssid", json_str_new(value));
    }
    else {
        json_obj_append(json, "wifi_bssid", json_str_new(""));
    }

    /* Current Wi-Fi info */
    int rssi = wifi_station_get_rssi();
    if (rssi < -100) {
        rssi = -100;
    }
    if (rssi > -30) {
        rssi = -30;
    }

    char current_bssid_str[18] = {0};
    if (wifi_station_is_connected()) {
        snprintf(current_bssid_str, sizeof(current_bssid_str),
                "%02X:%02X:%02X:%02X:%02X:%02X", BSSID2STR(wifi_get_bssid_current()));
    }
    json_obj_append(json, "wifi_bssid_current", json_str_new(current_bssid_str));

    int8  wifi_signal_strength = -1;
    if (wifi_station_is_connected()) {
        if (rssi >= WIFI_RSSI_EXCELLENT) {
            wifi_signal_strength = 3;
        }
        else if (rssi >= WIFI_RSSI_GOOD) {
            wifi_signal_strength = 2;
        }
        else if (rssi >= WIFI_RSSI_FAIR) {
            wifi_signal_strength = 1;
        }
        else {
            wifi_signal_strength = 0;
        }
    }
    json_obj_append(json, "wifi_signal_strength", json_int_new(wifi_signal_strength));

    json_obj_append(json, "frequency", json_int_new(system_get_cpu_freq()));
    json_obj_append(json, "mem_usage", json_int_new(100 - 100 * system_get_free_heap_size() / MAX_AVAILABLE_RAM));
    json_obj_append(json, "flash_size", json_int_new(system_get_flash_size() / 1024));

#ifdef _OTA
    json_obj_append(json, "firmware_auto_update", json_bool_new(device_flags & DEVICE_FLAG_OTA_AUTO_UPDATE));
    json_obj_append(json, "firmware_beta_enabled", json_bool_new(device_flags & DEVICE_FLAG_OTA_BETA_ENABLED));
#endif

    /* Flash id */
    char id[10];
    snprintf(id, 10, "%08x", spi_flash_get_id());
    json_obj_append(json, "flash_id", json_str_new(id));

    /* Chip id */
    snprintf(id, 10, "%08x", system_get_chip_id());
    json_obj_append(json, "chip_id", json_str_new(id));

    /* Config name & model */
    if (!device_config_model[0] || !strcmp(device_config_model, "default")) {
        json_obj_append(json, "config_name", json_str_new(FW_CONFIG_NAME));
    }
    else {
        char config_name[64];
        snprintf(config_name, 64, "%s/%s", FW_CONFIG_NAME, device_config_model);
        json_obj_append(json, "config_name", json_str_new(config_name));
    }
    json_obj_append(json, "config_model", json_str_new(device_config_model));

#ifdef _DEBUG
    json_obj_append(json, "debug", json_bool_new(TRUE));
#endif

    /* Sleep mode */
#ifdef _SLEEP
    if (sleep_get_wake_interval()) {
        snprintf(value, 256, "%d:%d", sleep_get_wake_interval(), sleep_get_wake_duration());
    }
    else {
        value[0] = 0;
    }
    json_obj_append(json, "sleep_mode", json_str_new(value));
#endif

    /* Battery */
#ifdef _BATTERY
    json_obj_append(json, "battery_level", json_int_new(battery_get_level()));
    json_obj_append(json, "battery_voltage", json_int_new(battery_get_voltage()));
#endif

    /* Attribute definitions */
    json_obj_append(json, "definitions", device_attrdefs_to_json());

    json_stringify(json);

    return json;
}


json_t *api_get_device(json_t *query_json, int *code) {
    json_t *response_json = NULL;

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(API_ACCESS_LEVEL_ADMIN);
    }

    DEBUG_API("returning device attributes");

    *code = 200;
    
    return device_to_json();
}

json_t *api_patch_device(json_t *query_json, json_t *request_json, int *code) {
    DEBUG_API("updating device attributes");

    json_t *response_json = json_obj_new();
    
    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(API_ACCESS_LEVEL_ADMIN);
    }

    if (json_get_type(request_json) != JSON_TYPE_OBJ) {
        return API_ERROR(400, "invalid-request");
    }

    int i;
    bool needs_reset = FALSE;
    bool config_model_changed = FALSE;
#ifdef _SLEEP
    bool needs_sleep_reset = FALSE;
#endif
    char *key;
    json_t *child;
    for (i = 0; i < json_obj_get_len(request_json); i++) {
        key = json_obj_key_at(request_json, i);
        child = json_obj_value_at(request_json, i);

        if (!strcmp(key, "name")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(key);
            }
            
            char *value = json_str_get(child);
            if (!validate_id(value)) {
                return INVALID_FIELD(key);
            }
            
            strncpy(device_name, value, API_MAX_DEVICE_NAME_LEN);
            device_name[API_MAX_DEVICE_NAME_LEN - 1] = 0;
            
            DEBUG_DEVICE("name set to \"%s\"", device_name);

            httpserver_set_name(device_name);
        }
        else if (!strcmp(key, "display_name")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(key);
            }

            strncpy(device_display_name, json_str_get(child), API_MAX_DEVICE_DISP_NAME_LEN);
            device_display_name[API_MAX_DEVICE_DISP_NAME_LEN - 1] = 0;
            
            DEBUG_DEVICE("display name set to \"%s\"", device_display_name);
        }
        else if (!strcmp(key, "admin_password")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(key);
            }
            
            char *password = json_str_get(child);
            char *password_hash = sha256_hex(password);
            strcpy(device_admin_password_hash, password_hash);
            free(password_hash);
            DEBUG_DEVICE("admin password set");
        }
        else if (!strcmp(key, "normal_password")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(key);
            }
            
            char *password = json_str_get(child);
            char *password_hash = sha256_hex(password);
            strcpy(device_normal_password_hash, password_hash);
            free(password_hash);
            DEBUG_DEVICE("normal password set");
        }
        else if (!strcmp(key, "viewonly_password")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(key);
            }
            
            char *password = json_str_get(child);
            char *password_hash = sha256_hex(password);
            strcpy(device_viewonly_password_hash, password_hash);
            free(password_hash);
            DEBUG_DEVICE("view-only password set");
        }
        else if (!strcmp(key, "frequency")) {
            if (json_get_type(child) != JSON_TYPE_INT) {
                return INVALID_FIELD(key);
            }

            int frequency = json_int_get(child);
            if (!validate_num(frequency, UNDEFINED, UNDEFINED, TRUE, 0, frequency_choices)) {
                return INVALID_FIELD(key);
            }

            system_update_cpu_freq(frequency);
            DEBUG_DEVICE("CPU frequency set to %d MHz", frequency);
        }
        else if (!strcmp(key, "ip_address")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(key);
            }

            char *ip_address_str = json_str_get(child);
            ip_addr_t ip_address = {0};
            if (ip_address_str[0]) { /* Manual */
                uint8 bytes[4];
                if (!validate_ip_address(ip_address_str, bytes)) {
                    return INVALID_FIELD(key);
                }

                IP4_ADDR(&ip_address, bytes[0], bytes[1], bytes[2], bytes[3]);
            }

            wifi_set_ip_address(ip_address);
            needs_reset = TRUE;
        }
        else if (!strcmp(key, "ip_netmask")) {
            if (json_get_type(child) != JSON_TYPE_INT) {
                return INVALID_FIELD(key);
            }

            int netmask = json_int_get(child);
            if (!validate_num(netmask, 0, 31, /* integer = */ TRUE, /* step = */ 0, /* choices = */ NULL)) {
                return INVALID_FIELD(key);
            }

            wifi_set_netmask(netmask);
            needs_reset = TRUE;
        }
        else if (!strcmp(key, "ip_gateway")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(key);
            }

            char *gateway_str = json_str_get(child);
            ip_addr_t gateway = {0};
            if (gateway_str[0]) { /* Manual */
                uint8 bytes[4];
                if (!validate_ip_address(gateway_str, bytes)) {
                    return INVALID_FIELD(key);
                }

                IP4_ADDR(&gateway, bytes[0], bytes[1], bytes[2], bytes[3]);
            }

            wifi_set_gateway(gateway);
            needs_reset = TRUE;
        }
        else if (!strcmp(key, "ip_dns")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(key);
            }

            char *dns_str = json_str_get(child);
            ip_addr_t dns = {0};
            if (dns_str[0]) { /* Manual */
                uint8 bytes[4];
                if (!validate_ip_address(dns_str, bytes)) {
                    return INVALID_FIELD(key);
                }

                IP4_ADDR(&dns, bytes[0], bytes[1], bytes[2], bytes[3]);
            }

            wifi_set_dns(dns);
            needs_reset = TRUE;
        }
        else if (!strcmp(key, "wifi_ssid")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(key);
            }

            char *ssid = json_str_get(child);
            if (!validate_wifi_ssid(ssid)) {
                return INVALID_FIELD(key);
            }

            wifi_set_ssid(ssid);
            needs_reset = TRUE;
        }
        else if (!strcmp(key, "wifi_key")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(key);
            }

            char *psk = json_str_get(child);
            if (!validate_wifi_key(psk)) {
                return INVALID_FIELD(key);
            }

            wifi_set_psk(psk);
            needs_reset = TRUE;
        }
        else if (!strcmp(key, "wifi_bssid")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(key);
            }

            char *bssid_str = json_str_get(child);
            uint8 bssid[WIFI_BSSID_LEN];
            if (!validate_wifi_bssid(bssid_str, bssid)) {
                return INVALID_FIELD(key);
            }

            wifi_set_bssid(bssid);
            needs_reset = TRUE;
        }
#ifdef _SLEEP
        else if (!strcmp(key, "sleep_mode")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(key);
            }

            char *sleep_mode_str = json_str_get(child);
            if (sleep_mode_str[0]) {
                int wake_interval;
                int wake_duration;
                if (!validate_str_sleep_mode(sleep_mode_str, &wake_interval, &wake_duration)) {
                    return INVALID_FIELD(key);
                }

                sleep_set_wake_interval(wake_interval);
                sleep_set_wake_duration(wake_duration);
            }
            else {
                sleep_set_wake_interval(0);
            }

            needs_sleep_reset = TRUE;
        }
#endif
#ifdef _OTA
        else if (!strcmp(key, "firmware_auto_update")) {
            if (json_get_type(child) != JSON_TYPE_BOOL) {
                return INVALID_FIELD(key);
            }

            if (json_bool_get(child)) {
                device_flags |= DEVICE_FLAG_OTA_AUTO_UPDATE;
                DEBUG_OTA("firmware auto update enabled");
            }
            else {
                device_flags &= ~DEVICE_FLAG_OTA_AUTO_UPDATE;
                DEBUG_OTA("firmware auto update disabled");
            }
        }
        else if (!strcmp(key, "firmware_beta_enabled")) {
            if (json_get_type(child) != JSON_TYPE_BOOL) {
                return INVALID_FIELD(key);
            }

            if (json_bool_get(child)) {
                device_flags |= DEVICE_FLAG_OTA_BETA_ENABLED;
                DEBUG_OTA("firmware beta enabled");
            }
            else {
                device_flags &= ~DEVICE_FLAG_OTA_BETA_ENABLED;
                DEBUG_OTA("firmware beta disabled");
            }
        }
#endif
        else if (!strcmp(key, "config_model")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(key);
            }

            char *model = json_str_get(child);
            if (!validate_str(model, device_config_model_choices)) {
                return INVALID_FIELD(key);
            }

            strncpy(device_config_model, model, API_MAX_DEVICE_CONFIG_MODEL_LEN);
            device_config_model[API_MAX_DEVICE_CONFIG_MODEL_LEN - 1] = 0;

            config_model_changed = TRUE;
            DEBUG_DEVICE("config model set to \"%s\"", device_config_model);
        }
        else if (!strcmp(key, "version") ||
                 !strcmp(key, "api_version") ||
                 !strcmp(key, "vendor") ||
#ifdef _OTA
                 !strcmp(key, "firmware") ||
#endif
#ifdef _BATTERY
                 !strcmp(key, "battery_level") ||
                 !strcmp(key, "battery_voltage") ||
#endif
                 !strcmp(key, "listen") ||
                 !strcmp(key, "uptime") ||
                 !strcmp(key, "mem_usage") ||
                 !strcmp(key, "flash_size") ||
                 !strcmp(key, "debug") ||
                 !strcmp(key, "chip_id") ||
                 !strcmp(key, "flash_id") ||
                 !strcmp(key, "config_name")) {

            return ATTR_NOT_MODIFIABLE(key);
        }
        else {
            return NO_SUCH_ATTRIBUTE(key);
        }
    }

    /* If the configuration model has changed, mark the device unconfigured so that it will automatically reconfigure */
    if (config_model_changed) {
        DEBUG_DEVICE("marking device as unconfigured");
        device_flags &= ~DEVICE_FLAG_CONFIGURED;
        config_start_provisioning();
    }

    config_mark_for_saving();
    
    if (needs_reset) {
        DEBUG_API("reset needed");

        system_reset(/* delayed = */ TRUE);
    }
#ifdef _SLEEP
    else if (needs_sleep_reset) {
        sleep_reset();
    }
#endif
    
    *code = 204;
    
    /* Add a device change event */
    event_push_device_update();

    return response_json;
}

json_t *api_post_reset(json_t *query_json, json_t *request_json, int *code) {
    json_t *response_json = json_obj_new();

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(API_ACCESS_LEVEL_ADMIN);
    }

    if (json_get_type(request_json) != JSON_TYPE_OBJ) {
        return API_ERROR(400, "invalid-request");
    }

    json_t *factory_json = json_obj_lookup_key(request_json, "factory");
    bool factory = FALSE;
    if (factory_json) {
        if (json_get_type(factory_json) != JSON_TYPE_BOOL) {
            return INVALID_FIELD("factory");
        }
        factory = json_bool_get(factory_json);
    }

    if (factory) {
        flashcfg_reset();
        wifi_reset();
    }

    *code = 204;

    system_reset(/* delayed = */ TRUE);
    
    return response_json;
}

#ifdef _OTA

json_t *api_get_firmware(json_t *query_json, int *code) {
    json_t *response_json = NULL;

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(API_ACCESS_LEVEL_ADMIN);
    }

    int ota_state = ota_current_state();

    if (ota_state == OTA_STATE_IDLE) {
        bool beta = device_flags & DEVICE_FLAG_OTA_BETA_ENABLED;
        json_t *beta_json = json_obj_lookup_key(query_json, "beta");
        if (beta_json) {
            beta = !strcmp(json_str_get(beta_json), "true");
        }

        if (!ota_get_latest(beta, on_ota_latest)) {
            response_json = json_obj_new();
            json_obj_append(response_json, "error", json_str_new("busy"));
            *code = 503;
        }
    }
    else {  /* OTA busy */
        char *ota_state_str = ota_states_str[ota_state];

        response_json = json_obj_new();
        json_obj_append(response_json, "version", json_str_new(FW_VERSION));
        json_obj_append(response_json, "status", json_str_new(ota_state_str));
    }

    return response_json;
}

json_t *api_patch_firmware(json_t *query_json, json_t *request_json, int *code) {
    json_t *response_json = NULL;

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(API_ACCESS_LEVEL_ADMIN);
    }

    if (json_get_type(request_json) != JSON_TYPE_OBJ) {
        return API_ERROR(400, "invalid-request");
    }
    
    json_t *version_json = json_obj_lookup_key(request_json, "version");
    json_t *url_json = json_obj_lookup_key(request_json, "url");
    if (!version_json && !url_json) {
        return MISSING_FIELD("version");
    }

    if (url_json) {  /* URL given */
        if (json_get_type(url_json) != JSON_TYPE_STR) {
            return INVALID_FIELD("url");
        }

        ota_perform_url(json_str_get(url_json), on_ota_perform);
    }
    else { /* Assuming version_json */
        if (json_get_type(version_json) != JSON_TYPE_STR) {
            return INVALID_FIELD("version");
        }

        ota_perform_version(json_str_get(version_json), on_ota_perform);
    }

    return NULL;
}

#endif /* _OTA */

json_t *api_get_access(json_t *query_json, int *code) {
    json_t *response_json = json_obj_new();

    switch (api_access_level) {
        case API_ACCESS_LEVEL_ADMIN:
            json_obj_append(response_json, "level", json_str_new("admin"));
            break;

        case API_ACCESS_LEVEL_NORMAL:
            json_obj_append(response_json, "level", json_str_new("normal"));
            break;

        case API_ACCESS_LEVEL_VIEWONLY:
            json_obj_append(response_json, "level", json_str_new("viewonly"));
            break;
    
        default:
            json_obj_append(response_json, "level", json_str_new("none"));
            break;
    }

    *code = 200;

    return response_json;
}

json_t *api_get_ports(json_t *query_json, int *code) {
    json_t *response_json = json_list_new();

    if (api_access_level < API_ACCESS_LEVEL_VIEWONLY) {
        return FORBIDDEN(API_ACCESS_LEVEL_VIEWONLY);
    }

    port_t **port = all_ports, *p;
    json_refs_ctx_t json_refs_ctx;
    json_refs_ctx_init(&json_refs_ctx, JSON_REFS_TYPE_PORTS_LIST);
    while ((p = *port++)) {
        DEBUG_API("returning attributes of port %s", p->id);
        json_list_append(response_json, port_to_json(p, &json_refs_ctx));
        json_refs_ctx.index++;
    }

    *code = 200;

    return response_json;
}

json_t *api_post_ports(json_t *query_json, json_t *request_json, int *code) {
    json_t *response_json = json_obj_new();

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(API_ACCESS_LEVEL_ADMIN);
    }

    if (json_get_type(request_json) != JSON_TYPE_OBJ) {
        return API_ERROR(400, "invalid-request");
    }

    if (virtual_find_unused_slot(/* occupy = */ FALSE) < 0) {
        DEBUG_API("adding virtual port: no more free slots");
        return API_ERROR(400, "too-many-ports");
    }

    int i;
    json_t *child, *c, *c2;
    port_t *new_port = zalloc(sizeof(port_t));
    char *choice;

    /* Will automatically allocate slot */
    new_port->slot = -1;

    /* id */
    child = json_obj_lookup_key(request_json, "id");
    if (!child) {
        free(new_port);
        return MISSING_FIELD("id");
    }
    if (json_get_type(child) != JSON_TYPE_STR) {
        free(new_port);
        return INVALID_FIELD("id");
    }
    if (!validate_id(json_str_get(child))) {
        free(new_port);
        return INVALID_FIELD("id");
    }
    if (port_find_by_id(json_str_get(child))) {
        free(new_port);
        return API_ERROR(400, "duplicate-port");
    }
    strncpy(new_port->id, json_str_get(child), PORT_MAX_ID_LEN + 1);

    DEBUG_API("adding virtual port: id = \"%s\"", new_port->id);

    /* type */
    child = json_obj_lookup_key(request_json, "type");
    if (!child) {
        free(new_port);
        return MISSING_FIELD("type");
    }
    if (json_get_type(child) != JSON_TYPE_STR) {
        free(new_port);
        return INVALID_FIELD("type");
    }
    char *type = json_str_get(child);

    if (!strcmp(type, "number")) {
        new_port->type = PORT_TYPE_NUMBER;
    }
    else if (!strcmp(type, "boolean")) {
        new_port->type = PORT_TYPE_BOOLEAN;
    }
    else {
        free(new_port);
        return INVALID_FIELD("type");
    }

    DEBUG_API("adding virtual port: type = \"%s\"", type);

    /* min */
    child = json_obj_lookup_key(request_json, "min");
    if (child) {
        if (json_get_type(child) == JSON_TYPE_INT) {
            new_port->min = json_int_get(child);
        }
        else if (json_get_type(child) == JSON_TYPE_DOUBLE) {
            new_port->min = json_double_get(child);
        }
        else {
            free(new_port);
            return INVALID_FIELD("min");
        }

        DEBUG_API("adding virtual port: min = %s", dtostr(new_port->min, -1));
    }
    else {
        new_port->min = UNDEFINED;
    }

    /* max */
    child = json_obj_lookup_key(request_json, "max");
    if (child) {
        if (json_get_type(child) == JSON_TYPE_INT) {
            new_port->max = json_int_get(child);
        }
        else if (json_get_type(child) == JSON_TYPE_DOUBLE) {
            new_port->max = json_double_get(child);
        }
        else {
            free(new_port);
            return INVALID_FIELD("max");
        }

        /* min must be <= max */
        if (!IS_UNDEFINED(new_port->min) && new_port->min > new_port->max) {
            free(new_port);
            return INVALID_FIELD("max");
        }

        DEBUG_API("adding virtual port: max = %s", dtostr(new_port->max, -1));
    }
    else {
        new_port->max = UNDEFINED;
    }

    /* integer */
    child = json_obj_lookup_key(request_json, "integer");
    if (child) {
        if (json_get_type(child) != JSON_TYPE_BOOL) {
            free(new_port);
            return INVALID_FIELD("integer");
        }

        new_port->integer = json_bool_get(child);

        DEBUG_API("adding virtual port: integer = %d", new_port->integer);
    }

    /* step */
    child = json_obj_lookup_key(request_json, "step");
    if (child) {
        if (json_get_type(child) == JSON_TYPE_INT) {
            new_port->step = json_int_get(child);
        }
        else if (json_get_type(child) == JSON_TYPE_DOUBLE) {
            new_port->step = json_double_get(child);
        }
        else {
            free(new_port);
            return INVALID_FIELD("step");
        }

        DEBUG_API("adding virtual port: step = %s", dtostr(new_port->step, -1));
    }
    else {
        new_port->step = UNDEFINED;
    }

    /* choices */
    child = json_obj_lookup_key(request_json, "choices");
    if (child && new_port->type == PORT_TYPE_NUMBER) {
        if (json_get_type(child) != JSON_TYPE_LIST) {
            free(new_port);
            return INVALID_FIELD("choices");
        }

        int len = json_list_get_len(child);
        if (len < 1 || len > 256) {
            free(new_port);
            return INVALID_FIELD("choices");
        }

        new_port->choices = zalloc((json_list_get_len(child) + 1) * sizeof(char *));
        for (i = 0; i < len; i++) {
            c = json_list_value_at(child, i);
            if (json_get_type(c) != JSON_TYPE_OBJ) {
                free_choices(new_port->choices);
                free(new_port);
                return INVALID_FIELD("choices");
            }

            /* value */
            c2 = json_obj_lookup_key(c, "value");
            if (!c2) {
                free_choices(new_port->choices);
                free(new_port);
                return INVALID_FIELD("choices");
            }

            if (json_get_type(c2) == JSON_TYPE_INT) {
                new_port->choices[i] = strdup(dtostr(json_int_get(c2), /* decimals = */ 0));
            }
            else if (json_get_type(c2) == JSON_TYPE_DOUBLE) {
                new_port->choices[i] = strdup(dtostr(json_double_get(c2), /* decimals = */ -1));
            }
            else {
                free_choices(new_port->choices);
                free(new_port);
                return INVALID_FIELD("choices");
            }

            /* display_name */
            c2 = json_obj_lookup_key(c, "display_name");
            if (c2) {
                if (json_get_type(c2) != JSON_TYPE_STR) {
                    free_choices(new_port->choices);
                    free(new_port);
                    return INVALID_FIELD("choices");
                }

                choice = new_port->choices[i];
                choice = realloc(choice, strlen(choice) + 2 + strlen(json_str_get(c2)));
                strcat(choice, ":");
                strcat(choice, json_str_get(c2));

                new_port->choices[i] = choice;
            }
        }

        new_port->choices[json_list_get_len(child)] = NULL;

        DEBUG_API("adding virtual port: %d choices", len);
    }

    /* Sequence */
    new_port->sequence_pos = -1;

    if (!virtual_port_register(new_port)) {
        return API_ERROR(500, "port registration failed");
    }

    /* Rebuild deps mask for all ports, as the new port might be among their deps */
    port_t *p, **ports = all_ports;
    while ((p = *ports++)) {
        port_rebuild_change_dep_mask(p);
    }

    config_mark_for_saving();
    event_push_port_add(new_port);

    *code = 201;

    json_refs_ctx_t json_refs_ctx;
    json_refs_ctx_init(&json_refs_ctx, JSON_REFS_TYPE_PORT);

    return port_to_json(new_port, &json_refs_ctx);
}

json_t *api_patch_port(port_t *port, json_t *query_json, json_t *request_json, int *code) {
    DEBUG_API("updating attributes of port %s", port->id);
    
    json_t *response_json = json_obj_new();

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(API_ACCESS_LEVEL_ADMIN);
    }

    if (json_get_type(request_json) != JSON_TYPE_OBJ) {
        return API_ERROR(400, "invalid-request");
    }

    int i;
    char *key;
    json_t *child;

    /* Handle enabled attribute first */
    key = "enabled";
    child = json_obj_pop_key(request_json, key);
    if (child) {
        if (json_get_type(child) != JSON_TYPE_BOOL) {
            json_free(child);
            return INVALID_FIELD(key);
        }

        if (json_bool_get(child) && !IS_ENABLED(port)) {
            port_enable(port);
        }
        else if (!json_bool_get(child) && IS_ENABLED(port)) {
            port_disable(port);
        }

        json_free(child);
    }

    /* Then handle port-specific attributes */
    if (port->attrdefs) {
        attrdef_t *a, **attrdefs = port->attrdefs;
        while ((a = *attrdefs++)) {
            key = a->name;
            child = json_obj_pop_key(request_json, key);
            if (child) {
                if (!a->modifiable) {
                    json_free(child);
                    return ATTR_NOT_MODIFIABLE(key);
                }

                switch (a->type) {
                    case ATTR_TYPE_BOOLEAN: {
                        if (json_get_type(child) != JSON_TYPE_BOOL) {
                            json_free(child);
                            return INVALID_FIELD(key);
                        }

                        bool value = json_bool_get(child);

                        ((int_setter_t) a->set)(port, a, value);

                        DEBUG_PORT(port, "%s set to %d", a->name, value);

                        break;
                    }

                    case ATTR_TYPE_NUMBER: {
                        if (json_get_type(child) != JSON_TYPE_INT &&
                            (json_get_type(child) != JSON_TYPE_DOUBLE || a->integer)) {
                            json_free(child);
                            return INVALID_FIELD(key);
                        }

                        double value = json_get_type(child) == JSON_TYPE_INT ?
                                       json_int_get(child) : json_double_get(child);
                        int idx = validate_num(value, a->min, a->max, a->integer, a->step, a->choices);
                        if (!idx) {
                            json_free(child);
                            return INVALID_FIELD(key);
                        }

                        if (a->choices) {
                            ((int_setter_t) a->set)(port, a, idx - 1);
                        }
                        else {
                            if (a->integer) {
                                ((int_setter_t) a->set)(port, a, (int) value);
                            }
                            else { /* float */
                                ((float_setter_t) a->set)(port, a, value);
                            }
                        }

                        DEBUG_PORT(port, "%s set to %s", a->name, dtostr(value, -1));

                        break;
                    }

                    case ATTR_TYPE_STRING: {
                        if (json_get_type(child) != JSON_TYPE_STR) {
                            json_free(child);
                            return INVALID_FIELD(key);
                        }

                        char *value = json_str_get(child);
                        int idx = validate_str(value, a->choices);
                        if (!idx) {
                            json_free(child);
                            return INVALID_FIELD(key);
                        }

                        if (a->choices) {
                            ((int_setter_t) a->set)(port, a, idx - 1);
                        }
                        else {
                            ((str_setter_t) a->set)(port, a, value);
                        }

                        DEBUG_PORT(port, "%s set to %s", a->name, value ? value : "");

                        break;
                    }
                }

                json_free(child);
            }
        }
    }

    /* Finally, handle common attributes */
    for (i = 0; i < json_obj_get_len(request_json); i++) {
        key = json_obj_key_at(request_json, i);
        child = json_obj_value_at(request_json, i);

        if (!strcmp(key, "display_name")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(key);
            }

            free(port->display_name);
            port->display_name = NULL;
            if (*json_str_get(child)) {
                port->display_name = strndup(json_str_get(child), PORT_MAX_DISP_NAME_LEN);
            }

            DEBUG_PORT(port, "display_name set to \"%s\"", port->display_name ? port->display_name : "");
        }
        else if (!strcmp(key, "unit") && (port->type == PORT_TYPE_NUMBER)) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(key);
            }

            free(port->unit);
            port->unit = NULL;
            if (*json_str_get(child)) {
                port->unit = strndup(json_str_get(child), PORT_MAX_UNIT_LEN);
            }

            DEBUG_PORT(port, "unit set to \"%s\"", port->unit ? port->unit : "");
        }
        else if (IS_OUTPUT(port) && !strcmp(key, "expression")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(key);
            }

            if (port->sexpr) {
                port_expr_remove(port);
            }

            if (port->sequence_pos >= 0) {
                port_sequence_cancel(port);
            }

            char *sexpr = json_str_get(child);

            /* Use auxiliary s to check if expression is not empty (contains non-space characters) */
            char *s = sexpr;
            while (isspace((int) *s)) {
                s++;
            }
            if (*s) {
                /* Parse & validate expression */
                if (strlen(sexpr) > API_MAX_EXPR_LEN) {
                    DEBUG_PORT(port, "expression is too long");
                    return INVALID_EXPRESSION("too-long", /* token = */ NULL, /* pos = */ -1);
                }

                expr_t *expr = expr_parse(port->id, sexpr, strlen(sexpr));
                if (!expr) {
                    return INVALID_EXPRESSION_FROM_ERROR();
                }

                if (expr_check_loops(expr, port) > 1) {
                    DEBUG_API("loop detected in expression \"%s\"", sexpr);
                    expr_free(expr);
                    return INVALID_EXPRESSION("circular-dependency", /* token = */ NULL, /* pos = */ -1);
                }

                port->sexpr = strdup(sexpr);

                if (IS_ENABLED(port)) {
                    port->expr = expr;
                    port_rebuild_change_dep_mask(port);
                }
                else {
                    expr_free(expr);
                }
            }

            DEBUG_PORT(port, "expression set to \"%s\"", port->sexpr ? port->sexpr : "");
            update_port_expression(port);
        }
        else if (IS_OUTPUT(port) && !strcmp(key, "transform_write")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(key);
            }

            if (port->transform_write) {
                DEBUG_PORT(port, "removing write transform");
                expr_free(port->transform_write);
                free(port->stransform_write);
                port->transform_write = NULL;
                port->stransform_write = NULL;
            }

            char *stransform_write = json_str_get(child);

            /* Use auxiliary s to check if expression is not empty (contains non-space characters) */
            char *s = stransform_write;
            while (isspace((int) *s)) {
                s++;
            }
            if (*s) {
                /* Parse & validate expression */
                if (strlen(stransform_write) > API_MAX_EXPR_LEN) {
                    DEBUG_PORT(port, "expression is too long");
                    return INVALID_EXPRESSION("too-long", /* token = */ NULL, /* pos = */ -1);
                }

                expr_t *transform_write = expr_parse(port->id, stransform_write, strlen(stransform_write));
                if (!transform_write) {
                    return INVALID_EXPRESSION_FROM_ERROR();
                }

                port_t **_ports, **ports, *p;
                port_t *other_dep = NULL;
                _ports = ports = expr_port_deps(transform_write);
                if (ports) {
                    while ((p = *ports++)) {
                        if (p == port) {
                            continue;
                        }
                        other_dep = p;
                        break;
                    }
                    free(_ports);
                }

                // FIXME: if referenced port is unavailable, external dependency is not detected
                // FIXME: using strstr() to find reference position may lead to erroneous results when a port id is a
                //        substring of another
                if (other_dep) {
                    DEBUG_PORT(port, "transform expression depends on external port \"%s\"", other_dep->id);
                    expr_free(transform_write);
                    int32 pos = strstr(stransform_write, other_dep->id) - stransform_write;
                    return INVALID_EXPRESSION("external-dependency", other_dep->id, pos);
                }

                port->stransform_write = strdup(stransform_write);
                port->transform_write = transform_write;

                DEBUG_PORT(port, "write transform set to \"%s\"", port->stransform_write ? port->stransform_write : "");
            }
        }
        else if (!strcmp(key, "transform_read")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(key);
            }

            if (port->transform_read) {
                DEBUG_PORT(port, "removing read transform");
                expr_free(port->transform_read);
                free(port->stransform_read);
                port->transform_read = NULL;
                port->stransform_read = NULL;
            }

            char *stransform_read = json_str_get(child);

            /* Use auxiliary s to check if expression is not empty (contains non-space characters) */
            char *s = stransform_read;
            while (isspace((int) *s)) {
                s++;
            }
            if (*s) {
                /* Parse & validate expression */
                if (strlen(stransform_read) > API_MAX_EXPR_LEN) {
                    DEBUG_PORT(port, "expression is too long");
                    return INVALID_EXPRESSION("too-long", /* token = */ NULL, /* pos = */ -1);
                }

                expr_t *transform_read = expr_parse(port->id, stransform_read, strlen(stransform_read));
                if (!transform_read) {
                    return INVALID_EXPRESSION_FROM_ERROR();
                }

                port_t **_ports, **ports, *p;
                port_t *other_dep = NULL;
                _ports = ports = expr_port_deps(transform_read);
                if (ports) {
                    while ((p = *ports++)) {
                        if (p == port) {
                            continue;
                        }
                        other_dep = p;
                        break;
                    }
                    free(_ports);
                }

                // FIXME: if referenced port is unavailable, external dependency is not detected
                // FIXME: using strstr() to find reference position may lead to erroneous results when a port id is a
                //        substring of another
                if (other_dep) {
                    DEBUG_PORT(port, "transform expression depends on external port \"%s\"", other_dep->id);
                    expr_free(transform_read);
                    int32 pos = strstr(stransform_read, other_dep->id) - stransform_read;
                    return INVALID_EXPRESSION("external-dependency", other_dep->id, pos);
                }

                port->stransform_read = strdup(stransform_read);
                port->transform_read = transform_read;

                DEBUG_PORT(port, "read transform set to \"%s\"", port->stransform_read ? port->stransform_read : "");
            }
        }
        else if (!strcmp(key, "persisted")) {
            if (json_get_type(child) != JSON_TYPE_BOOL) {
                return INVALID_FIELD(key);
            }

            if (json_bool_get(child)) {
                port->flags |= PORT_FLAG_PERSISTED;
                DEBUG_PORT(port, "persist enabled");
            }
            else {
                port->flags &= ~PORT_FLAG_PERSISTED;
                DEBUG_PORT(port, "persist disabled");
            }
        }
        else if (!strcmp(key, "internal")) {
            if (json_get_type(child) != JSON_TYPE_BOOL) {
                return INVALID_FIELD(key);
            }

            if (json_bool_get(child)) {
                port->flags |= PORT_FLAG_INTERNAL;
                DEBUG_PORT(port, "internal enabled");
            }
            else {
                port->flags &= ~PORT_FLAG_INTERNAL;
                DEBUG_PORT(port, "internal disabled");
            }
        }
        else if (!IS_VIRTUAL(port) && !strcmp(key, "sampling_interval")) {
            if (json_get_type(child) != JSON_TYPE_INT) {
                return INVALID_FIELD(key);
            }

            int sampling_interval = json_int_get(child);
            if (!validate_num(sampling_interval,
                              port->min_sampling_interval,
                              port->max_sampling_interval, TRUE, 0, NULL)) {

                return INVALID_FIELD(key);
            }

            port->sampling_interval = sampling_interval;

            DEBUG_PORT(port, "sampling interval set to %d ms", sampling_interval);
        }
        else if (!strcmp(key, "id") ||
                 !strcmp(key, "type") ||
                 !strcmp(key, "writable") ||
                 !strcmp(key, "min") ||
                 !strcmp(key, "max") ||
                 !strcmp(key, "integer") ||
                 !strcmp(key, "step") ||
                 !strcmp(key, "choices")) {

            return ATTR_NOT_MODIFIABLE(key);
        }
        else {
            return NO_SUCH_ATTRIBUTE(key);
        }
    }
    
    if (IS_ENABLED(port)) {
        port_configure(port);
    }

    /* Set device configured flag */
    DEBUG_DEVICE("mark device as configured");
    device_flags |= DEVICE_FLAG_CONFIGURED;

    config_mark_for_saving();
    event_push_port_update(port);

    *code = 204;

    return response_json;
}

json_t *api_delete_port(port_t *port, json_t *query_json, int *code) {
    DEBUG_API("deleting virtual port %s", port->id);

    json_t *response_json = json_obj_new();

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(API_ACCESS_LEVEL_ADMIN);
    }

    if (!IS_VIRTUAL(port)) {
        return API_ERROR(400, "port-not-removable");  /* Can't unregister a non-virtual port */
    }

    event_push_port_remove(port);

    /* Free choices */
    if (port->choices) {
        free_choices(port->choices);
        port->choices = NULL;
    }

    /* Removing virtual flag disables virtual port */
    port->flags &= ~PORT_FLAG_VIRTUAL_ACTIVE;

    /* Free display name & unit */
    if (port->display_name) {
        free(port->display_name);
        port->display_name = NULL;
    }
    if (port->unit) {
        free(port->unit);
        port->unit = NULL;
    }

    /* Destroy value expression */
    if (port->expr) {
        port_expr_remove(port);
    }

    /* Destroy transform expressions */
    if (port->transform_read) {
        expr_free(port->transform_read);
        port->transform_read = NULL;
    }
    if (port->stransform_read) {
        free(port->stransform_read);
        port->stransform_read = NULL;
    }
    if (port->transform_write) {
        expr_free(port->transform_write);
        port->transform_write = NULL;
    }
    if (port->stransform_write) {
        free(port->stransform_write);
        port->stransform_write = NULL;
    }

    /* Cancel sequence */
    if (port->sequence_pos >= 0) {
        port_sequence_cancel(port);
    }

    if (!virtual_port_unregister(port)) {
        return API_ERROR(500, "port unregister failed");
    }

    config_mark_for_saving();

    free(port);

    /* Rebuild deps mask for all ports, as the former port might have been among their deps */
    port_t *p, **ports = all_ports;
    while ((p = *ports++)) {
        port_rebuild_change_dep_mask(p);
    }

    *code = 204;

    return response_json;
}

json_t *api_get_port_value(port_t *port, json_t *query_json, int *code) {
    json_t *response_json = NULL;

    if (api_access_level < API_ACCESS_LEVEL_VIEWONLY) {
        return FORBIDDEN(API_ACCESS_LEVEL_VIEWONLY);
    }

    /* Poll ports before retrieving current port value, ensuring value is as up-to-date as possible */
    core_poll();

    response_json = port_get_json_value(port);

    *code = 200;

    return response_json;
}

json_t *api_patch_port_value(port_t *port, json_t *query_json, json_t *request_json, int *code) {
    json_t *response_json = json_obj_new();

    if (api_access_level < API_ACCESS_LEVEL_NORMAL) {
        return FORBIDDEN(API_ACCESS_LEVEL_NORMAL);
    }

    if (!IS_ENABLED(port)) {
        return API_ERROR(400, "port-disabled");
    }

    if (!IS_OUTPUT(port)) {
        return API_ERROR(400, "read-only-port");
    }

    if (port->type == PORT_TYPE_BOOLEAN) {
        if (json_get_type(request_json) != JSON_TYPE_BOOL) {
            return API_ERROR(400, "invalid-value");
        }

        if (!port_set_value(port, json_bool_get(request_json), CHANGE_REASON_API)) {
            return API_ERROR(400, "invalid-value");
        }
    }
    else if (port->type == PORT_TYPE_NUMBER) {
        if (json_get_type(request_json) != JSON_TYPE_INT &&
            (json_get_type(request_json) != JSON_TYPE_DOUBLE || port->integer)) {

            return API_ERROR(400, "invalid-value");
        }

        double value = json_get_type(request_json) == JSON_TYPE_INT ?
                       json_int_get(request_json) : json_double_get(request_json);

        if (!validate_num(value, port->min, port->max, port->integer, port->step, port->choices)) {
            return API_ERROR(400, "invalid-value");
        }

        if (!port_set_value(port, value, CHANGE_REASON_API)) {
            return API_ERROR(400, "invalid-value");
        }
    }

    *code = 204;
    
    return response_json;
}

json_t *api_patch_port_sequence(port_t *port, json_t *query_json, json_t *request_json, int *code) {
    json_t *response_json = NULL;

    if (api_access_level < API_ACCESS_LEVEL_NORMAL) {
        return FORBIDDEN(API_ACCESS_LEVEL_NORMAL);
    }

    if (!IS_ENABLED(port)) {
        return API_ERROR(400, "port-disabled");
    }

    if (!IS_OUTPUT(port)) {
        return API_ERROR(400, "read-only-port");
    }

    if (port->expr) {
        return API_ERROR(400, "port-with-expression");
    }

    json_t *values_json = json_obj_lookup_key(request_json, "values");
    if (!values_json) {
        return MISSING_FIELD("values");
    }
    if (json_get_type(values_json) != JSON_TYPE_LIST || !json_list_get_len(values_json)) {
        return INVALID_FIELD("values");
    }    

    json_t *delays_json = json_obj_lookup_key(request_json, "delays");
    if (!delays_json) {
        return MISSING_FIELD("delays");
    }
    if (json_get_type(delays_json) != JSON_TYPE_LIST || !json_list_get_len(delays_json)) {
        return INVALID_FIELD("delays");
    }
    if (json_list_get_len(delays_json) != json_list_get_len(values_json)) {
        return INVALID_FIELD("delays");
    }

    json_t *repeat_json = json_obj_lookup_key(request_json, "repeat");
    if (!repeat_json) {
        return MISSING_FIELD("repeat");
    }
    if (json_get_type(repeat_json) != JSON_TYPE_INT) {
        return INVALID_FIELD("repeat");
    }
    int repeat = json_int_get(repeat_json);
    if (repeat < API_MIN_SEQUENCE_REPEAT || repeat > API_MAX_SEQUENCE_REPEAT) {
        return INVALID_FIELD("repeat");
    }

    int i;
    json_t *j;
    
    if (port->sequence_pos >= 0) {
        port_sequence_cancel(port);
    }

    port->sequence_len = json_list_get_len(values_json);
    port->sequence_pos = 0;
    port->sequence_repeat = repeat;
    port->sequence_values = malloc(sizeof(double) * port->sequence_len);
    port->sequence_delays = malloc(sizeof(int) * (port->sequence_len));

    /* Values */
    for (i = 0; i < json_list_get_len(values_json); i++) {
        j = json_list_value_at(values_json, i);
        if (port->type == PORT_TYPE_BOOLEAN) {
            if (json_get_type(j) != JSON_TYPE_BOOL) {
                return SEQ_INVALID_FIELD("values");
            }

            port->sequence_values[i] = json_bool_get(j);
        }
        if (port->type == PORT_TYPE_NUMBER) {
            if (json_get_type(j) != JSON_TYPE_INT &&
                (json_get_type(j) != JSON_TYPE_DOUBLE || port->integer)) {

                return SEQ_INVALID_FIELD("values");
            }

            double value = json_get_type(j) == JSON_TYPE_INT ? json_int_get(j) : json_double_get(j);
            if (!validate_num(value, port->min, port->max, port->integer, port->step, port->choices)) {
                return SEQ_INVALID_FIELD("values");
            }

            port->sequence_values[i] = value;
        }
    }

    /* Delays */
    for (i = 0; i < json_list_get_len(delays_json); i++) {
        j = json_list_value_at(delays_json, i);
        if (json_get_type(j) != JSON_TYPE_INT) {
            return SEQ_INVALID_FIELD("delays");
        }
        
        port->sequence_delays[i] = json_int_get(j);
        
        if (port->sequence_delays[i] < API_MIN_SEQUENCE_DELAY || port->sequence_delays[i] > API_MAX_SEQUENCE_DELAY) {
            return SEQ_INVALID_FIELD("delays");
        }
    }
    
    /* Start sequence timer */
    os_timer_disarm(&port->sequence_timer);
    os_timer_setfn(&port->sequence_timer, on_sequence_timer, port);
    os_timer_arm(&port->sequence_timer, 1, /* repeat = */ FALSE);

    response_json = json_obj_new();
    *code = 204;
    
    return response_json;
}

json_t *api_get_webhooks(json_t *query_json, int *code) {
    json_t *response_json = json_obj_new();

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(API_ACCESS_LEVEL_ADMIN);
    }

    DEBUG_API("returning webhooks parameters");

    json_obj_append(response_json, "enabled", json_bool_new(device_flags & DEVICE_FLAG_WEBHOOKS_ENABLED));
    json_obj_append(response_json, "scheme",
                    json_str_new(device_flags & DEVICE_FLAG_WEBHOOKS_HTTPS ? "https" : "http"));
    json_obj_append(response_json, "host", json_str_new(webhooks_host ? webhooks_host : ""));
    json_obj_append(response_json, "port", json_int_new(webhooks_port));
    json_obj_append(response_json, "path", json_str_new(webhooks_path ? webhooks_path : ""));

    json_t *json_events = json_list_new();

    int e;
    for (e = EVENT_TYPE_MIN; e <= EVENT_TYPE_MAX; e++) {
        if (webhooks_events_mask & (1U << e)) {
            json_list_append(json_events, json_str_new(EVENT_TYPES_STR[e]));
        }
    }

    json_obj_append(response_json, "events", json_events);
    json_obj_append(response_json, "timeout", json_int_new(webhooks_timeout));
    json_obj_append(response_json, "retries", json_int_new(webhooks_retries));

    *code = 200;

    return response_json;
}

json_t *api_patch_webhooks(json_t *query_json, json_t *request_json, int *code) {
    DEBUG_API("updating webhooks parameters");

    json_t *response_json = json_obj_new();

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(API_ACCESS_LEVEL_ADMIN);
    }

    if (json_get_type(request_json) != JSON_TYPE_OBJ) {
        return API_ERROR(400, "invalid-request");
    }

    /* Scheme */
    json_t *scheme_json = json_obj_lookup_key(request_json, "scheme");
    if (scheme_json) {
        if (json_get_type(scheme_json) != JSON_TYPE_STR) {
            return INVALID_FIELD("scheme");
        }

#ifdef _SSL
        if (!strcmp(json_str_get(scheme_json), "https")) {
            device_flags |= DEVICE_FLAG_WEBHOOKS_HTTPS;
            DEBUG_WEBHOOKS("scheme set to HTTPS");
        }
        else
#endif
        if (!strcmp(json_str_get(scheme_json), "http")) {
            DEBUG_WEBHOOKS("scheme set to HTTP");
            device_flags &= ~DEVICE_FLAG_WEBHOOKS_HTTPS;
        }
        else {
            return INVALID_FIELD("scheme");
        }
    }

    /* Host */
    json_t *host_json = json_obj_lookup_key(request_json, "host");
    if (host_json) {
        if (json_get_type(host_json) != JSON_TYPE_STR) {
            return INVALID_FIELD("host");
        }
        if (strlen(json_str_get(host_json)) == 0) {
            return INVALID_FIELD("host");
        }

        if (webhooks_host) {
            free(webhooks_host);
        }

        webhooks_host = strdup(json_str_get(host_json));
        DEBUG_WEBHOOKS("host set to \"%s\"", webhooks_host);
    }

    /* Port */
    json_t *port_json = json_obj_lookup_key(request_json, "port");
    if (port_json) {
        if (json_get_type(port_json) != JSON_TYPE_INT) {
            return INVALID_FIELD("port");
        }

        if (json_int_get(port_json) < 0 || json_int_get(port_json) > 65535) {
            return INVALID_FIELD("port");
        }

        webhooks_port = json_int_get(port_json);
        DEBUG_WEBHOOKS("port set to %d", webhooks_port);
    }

    /* Path */
    json_t *path_json = json_obj_lookup_key(request_json, "path");
    if (path_json) {
        if (json_get_type(path_json) != JSON_TYPE_STR) {
            return INVALID_FIELD("path");
        }
        if (strlen(json_str_get(path_json)) == 0) {
            return INVALID_FIELD("path");
        }

        if (webhooks_path) {
            free(webhooks_path);
        }

        webhooks_path = strdup(json_str_get(path_json));
        DEBUG_WEBHOOKS("path set to \"%s\"", webhooks_path);
    }

    /* Password */
    json_t *password_json = json_obj_lookup_key(request_json, "password");
    if (password_json) {
        if (json_get_type(password_json) != JSON_TYPE_STR) {
            return INVALID_FIELD("password");
        }

        char *password = json_str_get(password_json);
        char *password_hash = sha256_hex(password);
        strcpy(webhooks_password_hash, password_hash);
        free(password_hash);
        DEBUG_WEBHOOKS("password set");
    }

    /* Events */
    json_t *event_json, *events_json = json_obj_lookup_key(request_json, "events");
    if (events_json) {
        if (json_get_type(events_json) != JSON_TYPE_LIST) {
            return INVALID_FIELD("events");
        }

        int i, e, len = json_list_get_len(events_json);
        uint8 events_mask = 0;
        for (i = 0; i < len; i++) {
            event_json = json_list_value_at(events_json, i);
            if (json_get_type(event_json) != JSON_TYPE_STR) {
                return INVALID_FIELD("events");
            }

            for (e = EVENT_TYPE_MIN; e <= EVENT_TYPE_MAX; e++) {
                if (!strcmp(json_str_get(event_json), EVENT_TYPES_STR[e])) {
                    events_mask |= (1U << e);
                    break;
                }
            }

            if (e > EVENT_TYPE_MAX) {
                return INVALID_FIELD("events");
            }

            DEBUG_WEBHOOKS("event mask includes %s", EVENT_TYPES_STR[e]);
        }

        webhooks_events_mask = events_mask;
    }

    /* Timeout */
    json_t *timeout_json = json_obj_lookup_key(request_json, "timeout");
    if (timeout_json) {
        if (json_get_type(timeout_json) != JSON_TYPE_INT) {
            return INVALID_FIELD("timeout");
        }

        if (json_int_get(timeout_json) < WEBHOOKS_MIN_TIMEOUT || json_int_get(timeout_json) > WEBHOOKS_MAX_TIMEOUT) {
            return INVALID_FIELD("timeout");
        }

        webhooks_timeout = json_int_get(timeout_json);
        DEBUG_WEBHOOKS("timeout set to %d", webhooks_timeout);
    }

    /* Retries */
    json_t *retries_json = json_obj_lookup_key(request_json, "retries");
    if (retries_json) {
        if (json_get_type(retries_json) != JSON_TYPE_INT) {
            return INVALID_FIELD("retries");
        }

        if (json_int_get(retries_json) < WEBHOOKS_MIN_RETRIES || json_int_get(retries_json) > WEBHOOKS_MAX_RETRIES) {
            return INVALID_FIELD("retries");
        }

        webhooks_retries = json_int_get(retries_json);
        DEBUG_WEBHOOKS("retries set to %d", webhooks_retries);
    }

    /* Enabled */
    json_t *enabled_json = json_obj_lookup_key(request_json, "enabled");
    if (enabled_json) {
        if (json_get_type(enabled_json) != JSON_TYPE_BOOL) {
            return INVALID_FIELD("enabled");
        }

        bool enabled = json_bool_get(enabled_json);
        if (enabled) {
            /* We can't enable webhooks unless we have a host and a path */
            if (!webhooks_host || !webhooks_host[0]) {
                return MISSING_FIELD("host");
            }
            if (!webhooks_path || !webhooks_path[0]) {
                return MISSING_FIELD("path");
            }

            device_flags |= DEVICE_FLAG_WEBHOOKS_ENABLED;
            DEBUG_WEBHOOKS("enabled");
        }
        else {
            device_flags &= ~DEVICE_FLAG_WEBHOOKS_ENABLED;
            DEBUG_WEBHOOKS("disabled");
        }
    }

    config_mark_for_saving();

    *code = 204;

    return response_json;
}

json_t *api_get_wifi(json_t *query_json, int *code) {
    json_t *response_json = NULL;

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(API_ACCESS_LEVEL_ADMIN);
    }

    if (!wifi_scan(on_wifi_scan)) {
        response_json = json_obj_new();
        json_obj_append(response_json, "error", json_str_new("busy"));
        *code = 503;

        return response_json;
    }

    return NULL;
}

json_t *api_get_raw_io(char *io, json_t *query_json, int *code) {
    json_t *response_json = json_obj_new();

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(API_ACCESS_LEVEL_ADMIN);
    }

    DEBUG_API("returning raw value for %s", io);

    if (!strncmp(io, "gpio", 4)) {
        int gpio_no = strtol(io + 4, NULL, 10);
        if (gpio_no < 0 || gpio_no > 16) {
            return API_ERROR(404, "no such io");
        }

        json_obj_append(response_json, "value", json_bool_new(gpio_read_value(gpio_no)));
        json_obj_append(response_json, "configured", json_bool_new(gpio_is_configured(gpio_no)));
        json_obj_append(response_json, "output", json_bool_new(gpio_is_output(gpio_no)));
        if (gpio_no == 16) {
            json_obj_append(response_json, "pull_down", json_bool_new(!gpio_get_pull(gpio_no)));
        }
        else {
            json_obj_append(response_json, "pull_up", json_bool_new(gpio_get_pull(gpio_no)));
        }
    }
    else if (!strcmp(io, "adc0")) {
        json_obj_append(response_json, "value", json_int_new(system_adc_read()));
    }
    else if (!strcmp(io, "hspi")) {
        uint8 bit_order;
        bool cpol;
        bool cpha;
        uint32 freq;
        bool configured = hspi_get_current_setup(&bit_order, &cpol, &cpha, &freq);

        json_obj_append(response_json, "bit_order", json_str_new(bit_order ? "lsb_first" : "msb_first"));
        json_obj_append(response_json, "cpol", json_bool_new(cpol));
        json_obj_append(response_json, "cpha", json_bool_new(cpha));
        json_obj_append(response_json, "freq", json_int_new(freq));
        json_obj_append(response_json, "configured", json_bool_new(configured));
    }
    else {
        return API_ERROR(404, "no such io");
    }

    *code = 200;

    return response_json;
}

json_t *api_patch_raw_io(char *io, json_t *query_json, json_t *request_json, int *code) {
    json_t *response_json = json_obj_new();
    json_t *value_json, *param_json;
    uint32 len, i;

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(API_ACCESS_LEVEL_ADMIN);
    }

    if (json_get_type(request_json) != JSON_TYPE_OBJ) {
        return API_ERROR(400, "invalid-request");
    }

    if (!strncmp(io, "gpio", 4)) {
        int gpio_no = strtol(io + 4, NULL, 10);
        if (gpio_no < 0 || gpio_no > 16) {
            return API_ERROR(404, "no such io");
        }

        json_t *value_json = json_obj_lookup_key(request_json, "value");
        bool value = gpio_read_value(gpio_no);
        if (value_json) {
            if (json_get_type(value_json) != JSON_TYPE_BOOL) {
                return INVALID_FIELD("value");
            }

            value = json_bool_get(value_json);
        }

        json_t *output_json = json_obj_lookup_key(request_json, "output");
        bool output = gpio_is_output(gpio_no);
        if (output_json) {
            if (json_get_type(output_json) != JSON_TYPE_BOOL) {
                return INVALID_FIELD("output");
            }

            output = json_bool_get(output_json);
        }

        json_t *pull_up_json = json_obj_lookup_key(request_json, "pull_up");
        bool pull_up = gpio_get_pull(gpio_no);
        if (pull_up_json) {
            if (json_get_type(pull_up_json) != JSON_TYPE_BOOL) {
                return INVALID_FIELD("pull_up");
            }

            pull_up = json_bool_get(pull_up_json);
        }

        json_t *pull_down_json = json_obj_lookup_key(request_json, "pull_down");
        bool pull_down = !gpio_get_pull(gpio_no);
        if (pull_down_json) {
            if (json_get_type(pull_down_json) != JSON_TYPE_BOOL) {
                return INVALID_FIELD("pull_down");
            }

            pull_down = json_bool_get(pull_down_json);
        }

        if (output) {
            gpio_configure_output(gpio_no, value);
        }
        else {
            if (gpio_no == 16) {
                gpio_configure_input(gpio_no, !pull_down);
            }
            else {
                gpio_configure_input(gpio_no, pull_up);
            }
        }
    }
    else if (!strcmp(io, "adc0")) {
        return API_ERROR(400, "cannot set adc value");
    }
    else if (!strcmp(io, "hspi")) {
        value_json = json_obj_lookup_key(request_json, "value");
        if (value_json) {
            if (json_get_type(value_json) != JSON_TYPE_LIST) {
                return INVALID_FIELD("value");
            }

            json_t *v_json;
            len = json_list_get_len(value_json);
            bool hex = FALSE;
            uint8 out_frame[len], in_frame[len];
            for (i = 0; i < len; i++) {
                v_json = json_list_value_at(value_json, i);
                if (json_get_type(v_json) == JSON_TYPE_INT) {
                    out_frame[i] = json_int_get(v_json) & 0xFF;
                }
                else if (json_get_type(v_json) == JSON_TYPE_STR) {
                    out_frame[i] = strtol(json_str_get(v_json), NULL, 16) & 0xFF;
                    hex = TRUE;
                }
                else {
                    return INVALID_FIELD("value");
                }
            }

            hspi_transfer(out_frame, in_frame, len);

            value_json = json_list_new();
            char hex_str[3];
            for (i = 0; i < len; i++) {
                if (hex) {
                    snprintf(hex_str, sizeof(hex_str), "%02X", in_frame[i]);
                    json_list_append(value_json, json_str_new(hex_str));
                }
                else {
                    json_list_append(value_json, json_int_new(in_frame[i]));
                }
            }

            json_obj_append(response_json, "value", value_json);

            *code = 200;
        }

        uint8 bit_order;
        bool cpol;
        bool cpha;
        uint32 freq;
        bool reconfigured = FALSE;
        hspi_get_current_setup(&bit_order, &cpol, &cpha, &freq);

        param_json = json_obj_lookup_key(request_json, "freq");
        if (param_json) {
            if (json_get_type(param_json) != JSON_TYPE_INT) {
                return INVALID_FIELD("freq");
            }

            freq = json_int_get(param_json);
            reconfigured = TRUE;
        }

        param_json = json_obj_lookup_key(request_json, "cpol");
        if (param_json) {
            if (json_get_type(param_json) != JSON_TYPE_BOOL) {
                return INVALID_FIELD("cpol");
            }

            cpol = json_bool_get(param_json);
            reconfigured = TRUE;
        }

        param_json = json_obj_lookup_key(request_json, "cpha");
        if (param_json) {
            if (json_get_type(param_json) != JSON_TYPE_BOOL) {
                return INVALID_FIELD("cpha");
            }

            cpha = json_bool_get(param_json);
            reconfigured = TRUE;
        }

        param_json = json_obj_lookup_key(request_json, "bit_order");
        if (param_json) {
            if (json_get_type(param_json) != JSON_TYPE_STR) {
                return INVALID_FIELD("bit_order");
            }

            bit_order = !strcmp(json_str_get(param_json), "lsb_first");
            reconfigured = TRUE;
        }

        if (reconfigured) {
            hspi_setup(bit_order, cpol, cpha, freq);
        }
    }
    else {
        return API_ERROR(404, "no such io");
    }

    *code = 204;

    return response_json;
}

json_t *port_attrdefs_to_json(port_t *port, json_refs_ctx_t *json_refs_ctx) {
    json_t *json = json_obj_new();
    json_t *attrdef_json;

    if (port->attrdefs) {
        int8 found_port_index = -1;
        if (json_refs_ctx->type == JSON_REFS_TYPE_PORTS_LIST) {
            /* Look through previous ports for ports having exact same attrdefs */
            port_t *p, **ports = all_ports;
            uint8 i = 0;
            while ((p = *ports++) && (p != port)) {
                if ((p->attrdefs == port->attrdefs) &&
                    (IS_OUTPUT(p) == IS_OUTPUT(port)) &&
                    (IS_VIRTUAL(p) == IS_VIRTUAL(port)) &&
                    (p->type == port->type)) {

                    found_port_index = i;
                    break;
                }

                i++;
            }
        }

        if (found_port_index >= 0) {  /* Found a port with exact same attrdefs */
            json_t *ref;
            ref = make_json_ref("#/%d/definitions", found_port_index);
#if defined(_DEBUG) && defined(_DEBUG_API)
            char *ref_str = json_str_get(json_obj_value_at(ref, 0));
            DEBUG_API("replacing \"%s.definitions\" with $ref \"%s\"", port->id, ref_str);
#endif
            json_free(json);

            return ref;
        }

        attrdef_t *a, **attrdefs = port->attrdefs;
        while ((a = *attrdefs++)) {
            char **choices = a->choices;
            char *found_attrdef_name = NULL;
            int8 found_port_index = -1;

            if (choices) {
                /* Look through previous ports (and current port as well) for attrdefs with same choices */
                lookup_port_attrdef_choices(choices, port, a, &found_port_index, &found_attrdef_name, json_refs_ctx);
                if (found_attrdef_name) {
                    /* Will be replaced by $ref */
                    choices = NULL;
                }
            }

            attrdef_json = attrdef_to_json(a->display_name ? a->display_name : "",
                                           a->description ? a->description : "",
                                           a->unit, a->type, a->modifiable,
                                           a->min, a->max, a->integer, a->step, choices, a->reconnect);

            /* If similar choices found, replace with $ref */
            if (found_attrdef_name) {
                json_t *ref = NULL;
                switch (json_refs_ctx->type) {
                    case JSON_REFS_TYPE_PORTS_LIST:
                        ref = make_json_ref("#/%d/definitions/%s/choices", found_port_index, found_attrdef_name);
                        break;

                    case JSON_REFS_TYPE_PORT:
                        ref = make_json_ref("#/definitions/%s/choices", found_attrdef_name);
                        break;

                    case JSON_REFS_TYPE_LISTEN_EVENTS_LIST:
                        ref = make_json_ref("#/%d/params/definitions/%s/choices",
                                            json_refs_ctx->index, found_attrdef_name);
                        break;

                    case JSON_REFS_TYPE_WEBHOOKS_EVENT:
                        ref = make_json_ref("#/params/definitions/%s/choices", found_attrdef_name);
                        break;
                }
#if defined(_DEBUG) && defined(_DEBUG_API)
                char *ref_str = json_str_get(json_obj_value_at(ref, 0));
                DEBUG_API("replacing \"%s.definitions.%s.choices\" with $ref \"%s\"",
                          port->id, found_attrdef_name, ref_str);
#endif
                json_obj_append(attrdef_json, "choices", ref);
            }

            json_stringify(attrdef_json);
            json_obj_append(json, a->name, attrdef_json);
        }
    }

    if (!IS_VIRTUAL(port)) {
        /* Sampling_interval attrdef */
        if (json_refs_ctx->type == JSON_REFS_TYPE_PORTS_LIST && json_refs_ctx->sampling_interval_port_index >= 0) {
            attrdef_json = make_json_ref("#/%d/definitions/sampling_interval",
                                         json_refs_ctx->sampling_interval_port_index);
#if defined(_DEBUG) && defined(_DEBUG_API)
            char *ref_str = json_str_get(json_obj_value_at(attrdef_json, 0));
            DEBUG_API("replacing \"%s.definitions.sampling_interval\" with $ref \"%s\"", port->id, ref_str);
#endif
        }
        else {
            attrdef_json = attrdef_to_json("Sampling Interval", "Indicates how often to read the port value.",
                                           "ms", ATTR_TYPE_NUMBER, /* modifiable = */ TRUE,
                                           port->min_sampling_interval, port->max_sampling_interval,
                                           /* integer = */ TRUE , /* step = */ 0, /* choices = */ NULL,
                                           /* reconnect = */ FALSE);
            if (json_refs_ctx->type == JSON_REFS_TYPE_PORTS_LIST) {
                json_refs_ctx->sampling_interval_port_index = json_refs_ctx->index;
            }
        }
        json_obj_append(json, "sampling_interval", attrdef_json);
    }

    json_stringify(json);

    return json;
}

json_t *device_attrdefs_to_json(void) {
    json_t *json = json_obj_new();
    json_t *attrdef_json;

    attrdef_json = attrdef_to_json("CPU Frequency", "The CPU frequency.", "MHz", ATTR_TYPE_NUMBER,
                                   /* modifiable = */TRUE, /* min = */ UNDEFINED, /* max = */ UNDEFINED,
                                   /* integer = */ TRUE, /* step = */ 0, frequency_choices, /* reconnect = */ FALSE);
    json_obj_append(json, "frequency", attrdef_json);

#ifdef _SLEEP
    attrdef_json = attrdef_to_json("Sleep Mode",
                                   "Controls the sleep mode; format is <wake_interval_minutes>:<wake_duration_seconds>."
                                   " Interval is between 1 and 10080 minutes; duration is between 10 and 3600 seconds. "
                                   "Empty string disables sleep mode.", /* unit = */ NULL, ATTR_TYPE_STRING,
                                   /* modifiable = */ TRUE, /* min = */ UNDEFINED, /* max = */ UNDEFINED,
                                   /* integer = */ FALSE, /* step = */ 0, /* choices = */ NULL,
                                   /* reconnect = */ FALSE);
    json_obj_append(json, "sleep_mode", attrdef_json);
#endif

#ifdef _OTA
    attrdef_json = attrdef_to_json("Firmware Auto-update",
                                   "Enables automatic firmware update when detecting a newer version.",
                                   /* unit = */ NULL, ATTR_TYPE_BOOLEAN, /* modifiable = */ TRUE, /* min = */ UNDEFINED,
                                   /* max = */ UNDEFINED, /* integer = */ FALSE, /* step = */ 0, /* choices = */ NULL,
                                   /* reconnect = */ FALSE);
    json_obj_append(json, "firmware_auto_update", attrdef_json);

    attrdef_json = attrdef_to_json("Firmware Beta Versions",
                                   "Enables access to beta firmware releases. "
                                   "Leave this disabled unless you know what you're doing.",
                                   /* unit = */ NULL, ATTR_TYPE_BOOLEAN, /* modifiable = */ TRUE, /* min = */ UNDEFINED,
                                   /* max = */ UNDEFINED, /* integer = */ FALSE, /* step = */ 0, /* choices = */ NULL,
                                   /* reconnect = */ FALSE);
    json_obj_append(json, "firmware_beta_enabled", attrdef_json);
#endif

#ifdef _BATTERY
    attrdef_json = attrdef_to_json("Battery Voltage", "The battery voltage.", "mV", ATTR_TYPE_NUMBER,
                                   /* modifiable = */ FALSE, /* min = */ UNDEFINED, /* max = */ UNDEFINED,
                                   /* integer = */ FALSE, /* step = */ 0, /* choices = */ NULL,
                                   /* reconnect = */ FALSE);
    json_obj_append(json, "battery_voltage", attrdef_json);
#endif

    attrdef_json = attrdef_to_json("Flash Size", "Total flash memory size.", "kB", ATTR_TYPE_NUMBER,
                                   /* modifiable = */ FALSE, /* min = */ UNDEFINED, /* max = */ UNDEFINED,
                                   /* integer = */ TRUE, /* step = */ 0, /* choices = */ NULL, /* reconnect = */ FALSE);
    json_obj_append(json, "flash_size", attrdef_json);

    attrdef_json = attrdef_to_json("Flash ID", "Device flash model identifier.", /* unit = */ NULL, ATTR_TYPE_STRING,
                                   /* modifiable = */ FALSE, /* min = */ UNDEFINED, /* max = */ UNDEFINED,
                                   /* integer = */ FALSE, /* step = */ 0,  /* choices = */ NULL,
                                   /* reconnect = */ FALSE);
    json_obj_append(json, "flash_id", attrdef_json);

    attrdef_json = attrdef_to_json("Chip ID", "Device chip identifier.", /* unit = */ NULL, ATTR_TYPE_STRING,
                                   /* modifiable = */ FALSE, /* min = */ UNDEFINED, /* max = */ UNDEFINED,
                                   /* integer = */ FALSE, /* step = */ 0, /* choices = */ NULL,
                                   /* reconnect = */ FALSE);
    json_obj_append(json, "chip_id", attrdef_json);

    attrdef_json = attrdef_to_json("Configuration Model", "Device configuration model.", /* unit = */ NULL,
                                   ATTR_TYPE_STRING, /* modifiable = */ TRUE, /* min = */ UNDEFINED,
                                   /* max = */ UNDEFINED, /* integer = */ FALSE, /* step = */ 0,
                                   device_config_model_choices, /* reconnect = */ FALSE);
    json_obj_append(json, "config_model", attrdef_json);

#ifdef _DEBUG
    attrdef_json = attrdef_to_json("Debug", "Indicates that debugging was enabled when the firmware was built.",
                                   /* unit = */ NULL, ATTR_TYPE_BOOLEAN, /* modifiable = */ FALSE,
                                   /* min = */ UNDEFINED, /* max = */ UNDEFINED, /* integer = */ FALSE, /* step = */ 0,
                                   /* choices = */ NULL, /* reconnect = */ FALSE);
    json_obj_append(json, "debug", attrdef_json);
#endif

    return json;
}

void on_sequence_timer(void *arg) {
    port_t *port = arg;

    if (port->sequence_pos < port->sequence_len) {
        port_set_value(port, port->sequence_values[port->sequence_pos], CHANGE_REASON_SEQUENCE);

        DEBUG_PORT(port, "sequence delay of %d ms", port->sequence_delays[port->sequence_pos]);

        os_timer_arm(&port->sequence_timer, port->sequence_delays[port->sequence_pos], /* repeat = */ FALSE);
        port->sequence_pos++;
    }
    else { /* Sequence ended */
        if (port->sequence_repeat > 1 || port->sequence_repeat == 0) { /* Must repeat */
            if (port->sequence_repeat) {
                port->sequence_repeat--;
            }

            DEBUG_PORT(port, "repeating sequence (%d remaining iterations)", port->sequence_repeat);

            port->sequence_pos = 0;
            on_sequence_timer(arg);
        }
        else { /* Single iteration or repeat ended */
            os_timer_disarm(&port->sequence_timer);
            free(port->sequence_values);
            free(port->sequence_delays);
            port->sequence_pos = -1;
            port->sequence_repeat = -1;

            DEBUG_PORT(port, "sequence done");

            if (IS_PERSISTED(port)) {
                config_mark_for_saving();
            }
        }
    }
}

#ifdef _OTA

void on_ota_latest(char *version, char *date, char *url) {
    if (api_conn) {
        json_t *response_json = json_obj_new();
        json_obj_append(response_json, "version", json_str_new(FW_VERSION));
        json_obj_append(response_json, "status", json_str_new(ota_states_str[OTA_STATE_IDLE]));

        if (version) {
            json_obj_append(response_json, "latest_version", json_str_new(version));
            json_obj_append(response_json, "latest_date", json_str_new(date));
            json_obj_append(response_json, "latest_url", json_str_new(url));
        }

        respond_json(api_conn, 200, response_json);
        api_conn_reset();
    }

    if (version) {
        free(version);
        free(date);
        free(url);
    }
}

void on_ota_perform(int code) {
    if (!api_conn) {
        return;
    }

    if (code >= 200 && code < 300) {
        json_t *response_json = json_obj_new();
        respond_json(api_conn, 204, response_json);

        sessions_respond_all();
    }
    else {  /* Error */
        json_t *response_json = json_obj_new();
        json_obj_append(response_json, "error", json_str_new("no-such-version"));
        respond_json(api_conn, 404, response_json);
    }

    api_conn_reset();
}

#endif  /* _OTA */

void on_wifi_scan(wifi_scan_result_t *results, int len) {
    DEBUG_API("got wifi scan results");

    if (!api_conn) {
        return;  /* Such is life */
    }

    json_t *response_json = json_list_new();
    json_t *result_json;
    int i;
    char bssid[18];
    char *auth;

    for (i = 0; i < len; i++) {
        result_json = json_obj_new();
        json_obj_append(result_json, "ssid", json_str_new(results[i].ssid));
        snprintf(bssid, sizeof(bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
                 results[i].bssid[0], results[i].bssid[1], results[i].bssid[2],
                 results[i].bssid[3], results[i].bssid[4], results[i].bssid[5]);
        json_obj_append(result_json, "bssid", json_str_new(bssid));
        json_obj_append(result_json, "channel", json_int_new(results[i].channel));
        json_obj_append(result_json, "rssi", json_int_new(results[i].rssi));

        switch (results[i].auth_mode) {
            case AUTH_OPEN:
                auth = "OPEN";
                break;
            case AUTH_WEP:
                auth = "WEP";
                break;
            case AUTH_WPA_PSK:
                auth = "WPA_PSK";
                break;
            case AUTH_WPA2_PSK:
                auth = "WPA2_PSK";
                break;
            case AUTH_WPA_WPA2_PSK:
                auth = "WPA_WPA2_PSK";
                break;
            default:
                auth = "unknown";
        }
        json_obj_append(result_json, "auth_mode", json_str_new(auth));

        json_list_append(response_json, result_json);
    }

    free(results);

    respond_json(api_conn, 200, response_json);
    api_conn_reset();
}
