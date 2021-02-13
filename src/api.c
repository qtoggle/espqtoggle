
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

#include "espgoodies/battery.h"
#include "espgoodies/common.h"
#include "espgoodies/crypto.h"
#include "espgoodies/drivers/gpio.h"
#include "espgoodies/drivers/hspi.h"
#include "espgoodies/flashcfg.h"
#include "espgoodies/httpserver.h"
#include "espgoodies/ota.h"
#include "espgoodies/sleep.h"
#include "espgoodies/system.h"
#include "espgoodies/utils.h"
#include "espgoodies/wifi.h"


#include "apiutils.h"
#include "client.h"
#include "common.h"
#include "config.h"
#include "core.h"
#include "device.h"
#include "jsonrefs.h"
#include "peripherals.h"
#include "ports.h"
#include "sessions.h"
#include "ver.h"
#include "virtual.h"
#include "webhooks.h"
#include "api.h"


#define API_ERROR(response_json, c, error) ({     \
    *code = c;                                    \
    _api_error(response_json, error, NULL, NULL); \
})

#define FORBIDDEN(response_json, level) ({  \
    *code = 403;                            \
    _forbidden_error(response_json, level); \
})

#define MISSING_FIELD(response_json, field) ({                  \
    *code = 400;                                                \
    _api_error(response_json, "missing-field", "field", field); \
})

#define INVALID_FIELD(response_json, field) ({                  \
    *code = 400;                                                \
    _api_error(response_json, "invalid-field", "field", field); \
})

#define ATTR_NOT_MODIFIABLE(response_json, attr) ({                           \
    *code = 400;                                                              \
    _api_error(response_json, "attribute-not-modifiable", "attribute", attr); \
})

#define NO_SUCH_ATTR(response_json, attr) ({                           \
    *code = 400;                                                       \
    _api_error(response_json, "no-such-attribute", "attribute", attr); \
})

#define SEQ_INVALID_FIELD(response_json, field) ({ \
    free(port->sequence_values);                   \
    free(port->sequence_delays);                   \
    port->sequence_pos = -1;                       \
    port->sequence_repeat = -1;                    \
    INVALID_FIELD(response_json, field);           \
})

#define INVALID_EXPRESSION(response_json, field, reason, token, pos) ({   \
    *code = 400;                                                          \
    _invalid_expression_error(response_json, field, reason, token, pos);  \
})

#define INVALID_EXPRESSION_FROM_ERROR(response_json, field) ({ \
    expr_parse_error_t *parse_error = expr_parse_get_error();  \
    INVALID_EXPRESSION(                                        \
        response_json,                                         \
        field,                                                 \
        parse_error->reason,                                   \
        parse_error->token,                                    \
        parse_error->pos                                       \
    );                                                         \
})


#define RESPOND_NO_SUCH_FUNCTION(response_json) {                      \
    response_json = API_ERROR(response_json, 404, "no-such-function"); \
    goto response;                                                     \
}

#define WIFI_RSSI_EXCELLENT -55
#define WIFI_RSSI_GOOD      -65
#define WIFI_RSSI_FAIR      -75


static uint8           api_access_level;
static uint8           api_access_level_saved;
static struct espconn *api_conn;
static struct espconn *api_conn_saved;


#ifdef _OTA
static char *ota_states_str[] = {"idle", "checking", "downloading", "restarting"};
#endif

static json_t ICACHE_FLASH_ATTR *_api_error(json_t *response_json, char *error, char *field_name, char *field_value);
static json_t ICACHE_FLASH_ATTR *_forbidden_error(json_t *response_json, uint8 level);
static json_t ICACHE_FLASH_ATTR *_invalid_expression_error(
                                     json_t *response_json,
                                     char *field,
                                     char *reason,
                                     char *token,
                                     int32 pos
                                 );

static json_t ICACHE_FLASH_ATTR *device_from_json(
                                     json_t *json,
                                     int *code,
                                     bool *needs_reset,
                                     bool *needs_sleep_reset,
                                     bool *config_name_changed,
                                     bool ignore_unknown
                                 );
static json_t ICACHE_FLASH_ATTR *port_attrdefs_to_json(port_t *port, json_refs_ctx_t *json_refs_ctx);
static json_t ICACHE_FLASH_ATTR *device_attrdefs_to_json(void);

static void   ICACHE_FLASH_ATTR  on_sequence_timer(void *arg);

#ifdef _OTA
static void   ICACHE_FLASH_ATTR  on_ota_latest(char *version, char *date, char *url);
static void   ICACHE_FLASH_ATTR  on_ota_perform(int code);
#endif

static void   ICACHE_FLASH_ATTR  on_wifi_scan(wifi_scan_result_t *results, int len);


json_t *api_call_handle(int method, char* path, json_t *query_json, json_t *request_json, int *code) {
    char *part1 = NULL, *part2 = NULL, *part3 = NULL;
    char *token;
    *code = 200;

    json_t *response_json = NULL;
    port_t *port;

    if (method == HTTP_METHOD_OTHER) {
        RESPOND_NO_SUCH_FUNCTION(response_json);
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
        RESPOND_NO_SUCH_FUNCTION(response_json);
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
        RESPOND_NO_SUCH_FUNCTION(response_json);
    }

    /* Determine the API endpoint and method */

    if (!strcmp(part1, "device")) {
        if (part2) {
            RESPOND_NO_SUCH_FUNCTION(response_json);
        }
        else {
            if (method == HTTP_METHOD_GET) {
                response_json = api_get_device(query_json, code);
            }
            else if (method == HTTP_METHOD_PATCH) {
                response_json = api_patch_device(query_json, request_json, code);
            }
            else if (method == HTTP_METHOD_PUT) {
                response_json = api_put_device(query_json, request_json, code);
            }
            else {
                RESPOND_NO_SUCH_FUNCTION(response_json);
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
            RESPOND_NO_SUCH_FUNCTION(response_json);
        }
    }
#endif
    else if (!strcmp(part1, "access")) {
        if (!part2) {
            response_json = api_get_access(query_json, code);
        }
        else {
            RESPOND_NO_SUCH_FUNCTION(response_json);
        }
    }
    else if (!strcmp(part1, "backup")) {
        if (part2) {
            if (!strcmp(part2, "endpoints")) {
                response_json = api_get_backup_endpoints(query_json, code);
            }
            else {
                RESPOND_NO_SUCH_FUNCTION(response_json);
            }
        }
        else {
            RESPOND_NO_SUCH_FUNCTION(response_json);
        }
    }
    else if (!strcmp(part1, "ports")) {
        if (part2) {
            port = port_find_by_id(part2);
            if (!port) {
                response_json = API_ERROR(response_json, 404, "no-such-port");
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
                        RESPOND_NO_SUCH_FUNCTION(response_json);
                    }
                }
                else if (!strcmp(part3, "sequence")) { /* /ports/{id}/sequence */
                    if (method == HTTP_METHOD_PATCH) {
                        response_json = api_patch_port_sequence(port, query_json, request_json, code);
                    }
                    else {
                        RESPOND_NO_SUCH_FUNCTION(response_json);
                    }
                }
                else {
                    RESPOND_NO_SUCH_FUNCTION(response_json);
                }
            }
            else { /* /ports/{id} */
                if (method == HTTP_METHOD_PATCH) {
                    response_json = api_patch_port(port, query_json, request_json, code, /* provisioning = */ FALSE);
                }
                else if (method == HTTP_METHOD_DELETE) {
                    response_json = api_delete_port(port, query_json, code);
                }
                else {
                    RESPOND_NO_SUCH_FUNCTION(response_json);
                }
            }
        }
        else { /* /ports */
            if (method == HTTP_METHOD_GET) {
                response_json = api_get_ports(query_json, code);
            }
            else if (method == HTTP_METHOD_POST) {
                response_json = api_post_ports(query_json, request_json, code);
            }
            else if (method == HTTP_METHOD_PUT) {
                response_json = api_put_ports(query_json, request_json, code);
            }
            else {
                RESPOND_NO_SUCH_FUNCTION(response_json);
            }
        }
    }
    else if (!strcmp(part1, "webhooks")) {
        if (part2) {
            RESPOND_NO_SUCH_FUNCTION(response_json);
        }

        if (method == HTTP_METHOD_GET) {
            response_json = api_get_webhooks(query_json, code);
        }
        else if (method == HTTP_METHOD_PUT) {
            response_json = api_put_webhooks(query_json, request_json, code);
        }
        else {
            RESPOND_NO_SUCH_FUNCTION(response_json);
        }
    }
    else if (!strcmp(part1, "wifi")) {
        if (part2) {
            RESPOND_NO_SUCH_FUNCTION(response_json);
        }

        if (method == HTTP_METHOD_GET) {
            response_json = api_get_wifi(query_json, code);
        }
        else {
            RESPOND_NO_SUCH_FUNCTION(response_json);
        }
    }
    else if (!strcmp(part1, "rawio")) {
        if (part2) {
            if (part3) {
                RESPOND_NO_SUCH_FUNCTION(response_json);
            }

            if (method == HTTP_METHOD_GET) {
                response_json = api_get_raw_io(part2, query_json, code);
            }
            else if (method == HTTP_METHOD_PATCH) {
                response_json = api_patch_raw_io(part2, query_json, request_json, code);
            }
            else {
                RESPOND_NO_SUCH_FUNCTION(response_json);
            }
        }
        else {
            RESPOND_NO_SUCH_FUNCTION(response_json);
        }
    }
    else if (!strcmp(part1, "peripherals")) {
        if (part2) {
            RESPOND_NO_SUCH_FUNCTION(response_json);
        }

        if (method == HTTP_METHOD_GET) {
            response_json = api_get_peripherals(query_json, code);
        }
        else if (method == HTTP_METHOD_PUT) {
            response_json = api_put_peripherals(query_json, request_json, code);
        }
        else {
            RESPOND_NO_SUCH_FUNCTION(response_json);
        }
    }
    else if (!strcmp(part1, "system")) {
        if (part2) {
            RESPOND_NO_SUCH_FUNCTION(response_json);
        }

        if (method == HTTP_METHOD_GET) {
            response_json = api_get_system(query_json, code);
        }
        else if (method == HTTP_METHOD_PATCH) {
            response_json = api_patch_system(query_json, request_json, code);
        }
        else {
            RESPOND_NO_SUCH_FUNCTION(response_json);
        }
    }
    else if (!strcmp(part1, "provisioning")) {
        if (part2) {
            RESPOND_NO_SUCH_FUNCTION(response_json);
        }

        if (method == HTTP_METHOD_GET) {
            response_json = api_get_provisioning(query_json, code);
        }
        else if (method == HTTP_METHOD_PUT) {
            response_json = api_put_provisioning(query_json, request_json, code);
        }
        else {
            RESPOND_NO_SUCH_FUNCTION(response_json);
        }
    }
    else {
        RESPOND_NO_SUCH_FUNCTION(response_json);
    }

    response:

    free(part1);
    free(part2);
    free(part3);

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
    json_obj_append(json, "enabled", json_bool_new(IS_PORT_ENABLED(port)));
    json_obj_append(json, "writable", json_bool_new(IS_PORT_WRITABLE(port)));
    json_obj_append(json, "persisted", json_bool_new(IS_PORT_PERSISTED(port)));
    json_obj_append(json, "internal", json_bool_new(IS_PORT_INTERNAL(port)));

    if (IS_PORT_VIRTUAL(port)) {
        json_obj_append(json, "virtual", json_bool_new(TRUE));
    }

    if (!IS_PORT_VIRTUAL(port) && port->min_sampling_interval < port->max_sampling_interval) {
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
                port_t *p = NULL;
                uint8 i;
                for (i = 0; i < all_ports_count && p != port; i++) {
                    p = all_ports[i];
                    if (choices_equal(port->choices, p->choices)) {
                        found_ref_index = i;
                        break;
                    }
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

    /* Specific to writable ports */
    if (IS_PORT_WRITABLE(port)) {
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
                        if (IS_ATTRDEF_INTEGER(a)) {
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
    json_obj_append(json, "value", port_make_json_value(port));

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

    if (!strncmp(device_admin_password_hash, EMPTY_SHA256_HEX, SHA256_HEX_LEN)) {
        json_obj_append(json, "admin_password", json_str_new(""));
    }
    else {
        json_obj_append(json, "admin_password", json_str_new("set"));
    }

    if (!strncmp(device_normal_password_hash, EMPTY_SHA256_HEX, SHA256_HEX_LEN)) {
        json_obj_append(json, "normal_password", json_str_new(""));
    }
    else {
        json_obj_append(json, "normal_password", json_str_new("set"));
    }

    if (!strncmp(device_viewonly_password_hash, EMPTY_SHA256_HEX, SHA256_HEX_LEN)) {
        json_obj_append(json, "viewonly_password", json_str_new(""));
    }
    else {
        json_obj_append(json, "viewonly_password", json_str_new("set"));
    }

    /* Flags */
    json_t *flags_json = json_list_new();

    json_list_append(flags_json, json_str_new("expressions"));
#ifdef _OTA
    json_list_append(flags_json, json_str_new("firmware"));
#endif
    json_list_append(flags_json, json_str_new("backup"));
    json_list_append(flags_json, json_str_new("listen"));
    json_list_append(flags_json, json_str_new("sequences"));
#ifdef _SSL
    json_list_append(flags_json, json_str_new("ssl"));
#endif
    json_list_append(flags_json, json_str_new("webhooks"));

    json_obj_append(json, "flags", flags_json);

    /* Various optional attributes */
    json_obj_append(json, "uptime", json_int_new(system_uptime()));

    json_obj_append(json, "virtual_ports", json_int_new(VIRTUAL_MAX_PORTS));

    /* IP configuration */
    ip_addr_t ip = wifi_get_ip_address();
    if (ip.addr) {
        snprintf(value, 256, WIFI_IP_FMT, IP2STR(&ip));
        json_obj_append(json, "ip_address", json_str_new(value));
    }
    else {
        json_obj_append(json, "ip_address", json_str_new(""));
    }

    json_obj_append(json, "ip_netmask", json_int_new(wifi_get_netmask()));

    ip = wifi_get_gateway();
    if (ip.addr) {
        snprintf(value, 256, WIFI_IP_FMT, IP2STR(&ip));
        json_obj_append(json, "ip_gateway", json_str_new(value));
    }
    else {
        json_obj_append(json, "ip_gateway", json_str_new(""));
    }

    ip = wifi_get_dns();
    if (ip.addr) {
        snprintf(value, 256, WIFI_IP_FMT, IP2STR(&ip));
        json_obj_append(json, "ip_dns", json_str_new(value));
    }
    else {
        json_obj_append(json, "ip_dns", json_str_new(""));
    }

    /* Current IP info */
    ip = wifi_get_ip_address_current();
    if (ip.addr) {
        snprintf(value, 256, WIFI_IP_FMT, IP2STR(&ip));
        json_obj_append(json, "ip_address_current", json_str_new(value));
    }
    else {
        json_obj_append(json, "ip_address_current", json_str_new(""));
    }

    json_obj_append(json, "ip_netmask_current", json_int_new(wifi_get_netmask_current()));

    ip = wifi_get_gateway_current();
    if (ip.addr) {
        snprintf(value, 256, WIFI_IP_FMT, IP2STR(&ip));
        json_obj_append(json, "ip_gateway_current", json_str_new(value));
    }
    else {
        json_obj_append(json, "ip_gateway_current", json_str_new(""));
    }

    ip = wifi_get_dns_current();
    if (ip.addr) {
        snprintf(value, 256, WIFI_IP_FMT, IP2STR(&ip));
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
        snprintf(value, 256, "%02X:%02X:%02X:%02X:%02X:%02X", WIFI_BSSID2STR(bssid));
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
        snprintf(
            current_bssid_str,
            sizeof(current_bssid_str),
            "%02X:%02X:%02X:%02X:%02X:%02X",
            WIFI_BSSID2STR(wifi_get_bssid_current())
        );
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

    /* Configuration name */
    json_obj_append(json, "config_name", json_str_new(device_config_name));

#ifdef _DEBUG
    json_obj_append(json, "debug", json_bool_new(TRUE));
#endif

    /* Sleep mode */
#ifdef _SLEEP
    json_obj_append(json, "sleep_wake_interval", json_int_new(sleep_get_wake_interval()));
    json_obj_append(json, "sleep_wake_duration", json_int_new(sleep_get_wake_duration()));
#endif

    /* Battery */
#ifdef _BATTERY
    if (battery_enabled()) {
        json_obj_append(json, "battery_level", json_int_new(battery_get_level()));
        json_obj_append(json, "battery_voltage", json_int_new(battery_get_voltage()));
    }
#endif

    /* Attribute definitions */
    json_obj_append(json, "definitions", device_attrdefs_to_json());

    json_stringify(json);

    return json;
}

json_t *peripheral_to_json(peripheral_t *peripheral) {
    json_t *peripheral_json = json_obj_new();
    json_t *json;
    char flags_str[17];
    char hex[3];
    int i;

    /* Type */
    json_obj_append(peripheral_json, "type", json_int_new(peripheral->type_id));

    /* Flags */

    for (i = 0; i < 16; i++) {
        flags_str[i] = '0' + !!(peripheral->flags & BIT(15 - i));
    }
    flags_str[16] = '\0';
    json_obj_append(peripheral_json, "flags", json_str_new(flags_str));

    /* int8 params */
    json = json_list_new();
    json_obj_append(peripheral_json, "int8_params", json);
    for (i = 0; i < PERIPHERAL_MAX_INT8_PARAMS; i++) {
        snprintf(hex, sizeof(hex), "%02X", PERIPHERAL_PARAM_UINT8(peripheral, i));
        json_list_append(json, json_str_new(hex));
    }

    /* Port IDs */
    json = json_list_new();
    json_obj_append(peripheral_json, "port_ids", json);
    for (i = 0; i < all_ports_count; i++) {
        port_t *port = all_ports[i];
        if (port->peripheral != peripheral) {
            continue; /* Not our port */
        }

        json_list_append(json, json_str_new(port->id));
    }

    return peripheral_json;
}


json_t *api_get_device(json_t *query_json, int *code) {
    json_t *response_json = NULL;

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_ADMIN);
    }

    DEBUG_API("returning device attributes");

    *code = 200;
    
    return device_to_json();
}

json_t *api_put_device(json_t *query_json, json_t *request_json, int *code) {
    DEBUG_API("restoring device");

    json_t *response_json = NULL;
    
    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_ADMIN);
    }

    if (json_get_type(request_json) != JSON_TYPE_OBJ) {
        return API_ERROR(response_json, 400, "invalid-request");
    }

    /* Reset attributes to default values; network-related attributes will be left untouched, though */
    free(device_name);
    char default_device_name[API_MAX_DEVICE_NAME_LEN];
    snprintf(default_device_name, sizeof(default_device_name), DEFAULT_HOSTNAME, system_get_chip_id());
    device_name = strdup(default_device_name);

    free(device_display_name);
    device_display_name = strdup("");

#ifdef _SLEEP
    sleep_set_wake_interval(SLEEP_WAKE_INTERVAL_MIN);
    sleep_set_wake_duration(SLEEP_WAKE_DURATION_MIN);
#endif

#ifdef _OTA
    device_flags &= ~DEVICE_FLAG_OTA_AUTO_UPDATE;
    device_flags &= ~DEVICE_FLAG_OTA_BETA_ENABLED;
#endif

    device_config_name[0] = 0;

    /* Ignore any supplied clear-text password */
    json_free(json_obj_pop_key(request_json, "admin_password"));
    json_free(json_obj_pop_key(request_json, "normal_password"));
    json_free(json_obj_pop_key(request_json, "viewonly_password"));

    bool needs_reset, needs_sleep_reset, config_name_changed;
    response_json = device_from_json(
        request_json,
        code,
        &needs_reset,
        &needs_sleep_reset,
        &config_name_changed,
        /* ignore_unknown = */ TRUE
    );

    config_mark_for_saving();

    /* Inform consumers of the changes */
    event_push_device_update();

    return response_json;
}

json_t *api_patch_device(json_t *query_json, json_t *request_json, int *code) {
    DEBUG_API("updating device attributes");

    json_t *response_json = NULL;

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_ADMIN);
    }

    if (json_get_type(request_json) != JSON_TYPE_OBJ) {
        return API_ERROR(response_json, 400, "invalid-request");
    }

    bool needs_reset, needs_sleep_reset, config_name_changed;
    response_json = device_from_json(
        request_json,
        code,
        &needs_reset,
        &needs_sleep_reset,
        &config_name_changed,
        /* ignore_unknown = */ FALSE
    );

    if (config_name_changed) {
        /* If a new configuration name as been set, start the provisioning process */
        config_start_auto_provisioning();
    }

    config_mark_for_saving();
    
    if (needs_reset) {
        DEBUG_API("reset needed");

        system_reset(/* delayed = */ TRUE);
    }
#ifdef _SLEEP
    else if (needs_sleep_reset) {
        sleep_reset();

        /* Save configuration right away to prevent any losses due to sleep */
        config_ensure_saved();
    }
#endif
    
    /* Inform consumers of the changes */
    event_push_device_update();

    return response_json;
}

json_t *api_post_reset(json_t *query_json, json_t *request_json, int *code) {
    json_t *response_json = json_obj_new();

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_ADMIN);
    }

    if (json_get_type(request_json) != JSON_TYPE_OBJ) {
        return API_ERROR(response_json, 400, "invalid-request");
    }

    json_t *factory_json = json_obj_lookup_key(request_json, "factory");
    bool factory = FALSE;
    if (factory_json) {
        if (json_get_type(factory_json) != JSON_TYPE_BOOL) {
            return INVALID_FIELD(response_json, "factory");
        }
        factory = json_bool_get(factory_json);
    }

    if (factory) {
        config_factory_reset();
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
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_ADMIN);
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
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_ADMIN);
    }

    if (json_get_type(request_json) != JSON_TYPE_OBJ) {
        return API_ERROR(response_json, 400, "invalid-request");
    }
    
    json_t *version_json = json_obj_lookup_key(request_json, "version");
    json_t *url_json = json_obj_lookup_key(request_json, "url");
    if (!version_json && !url_json) {
        return MISSING_FIELD(response_json, "version");
    }

    if (url_json) {  /* URL given */
        if (json_get_type(url_json) != JSON_TYPE_STR) {
            return INVALID_FIELD(response_json, "url");
        }

        ota_perform_url(json_str_get(url_json), on_ota_perform);
    }
    else { /* Assuming version_json */
        if (json_get_type(version_json) != JSON_TYPE_STR) {
            return INVALID_FIELD(response_json, "version");
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

json_t *api_get_backup_endpoints(json_t *query_json, int *code) {
    json_t *response_json = json_list_new();

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_ADMIN);
    }

    json_t *peripherals_json = json_obj_new();
    json_obj_append(peripherals_json, "path", json_str_new("/peripherals"));
    json_obj_append(peripherals_json, "display_name", json_str_new("Peripherals"));
    json_obj_append(peripherals_json, "restore_method", json_str_new("PUT"));
    json_obj_append(peripherals_json, "order", json_int_new(5));

    json_t *system_json = json_obj_new();
    json_obj_append(system_json, "path", json_str_new("/system"));
    json_obj_append(system_json, "display_name", json_str_new("System Configuration"));
    json_obj_append(system_json, "restore_method", json_str_new("PATCH"));
    json_obj_append(system_json, "order", json_int_new(6));

    json_list_append(response_json, peripherals_json);
    json_list_append(response_json, system_json);

    return response_json;
}

json_t *api_get_ports(json_t *query_json, int *code) {
    json_t *response_json = json_list_new();

    if (api_access_level < API_ACCESS_LEVEL_VIEWONLY) {
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_VIEWONLY);
    }

    port_t *p;
    int i;
    json_refs_ctx_t json_refs_ctx;
    json_refs_ctx_init(&json_refs_ctx, JSON_REFS_TYPE_PORTS_LIST);
    for (i = 0; i < all_ports_count; i++) {
        p = all_ports[i];
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
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_ADMIN);
    }

    if (json_get_type(request_json) != JSON_TYPE_OBJ) {
        return API_ERROR(response_json, 400, "invalid-request");
    }

    if (virtual_find_unused_slot() < 0) {
        DEBUG_API("adding virtual port: no more free slots");
        return API_ERROR(response_json, 400, "too-many-ports");
    }

    int i;
    json_t *child, *c, *c2;
    port_t *new_port = port_new();
    char *choice;

    /* Will automatically allocate slot */
    new_port->slot = -1;

    /* id */
    child = json_obj_lookup_key(request_json, "id");
    if (!child) {
        free(new_port);
        return MISSING_FIELD(response_json, "id");
    }
    if (json_get_type(child) != JSON_TYPE_STR) {
        free(new_port);
        return INVALID_FIELD(response_json, "id");
    }
    if (!validate_id(json_str_get(child)) || strlen(json_str_get(child)) > PORT_MAX_ID_LEN) {
        free(new_port);
        return INVALID_FIELD(response_json, "id");
    }
    if (port_find_by_id(json_str_get(child))) {
        free(new_port);
        return API_ERROR(response_json, 400, "duplicate-port");
    }
    new_port->id = strdup(json_str_get(child));

    DEBUG_API("adding virtual port: id = \"%s\"", new_port->id);

    /* type */
    child = json_obj_lookup_key(request_json, "type");
    if (!child) {
        port_cleanup(new_port, /* free_id = */ TRUE);
        free(new_port);
        return MISSING_FIELD(response_json, "type");
    }
    if (json_get_type(child) != JSON_TYPE_STR) {
        port_cleanup(new_port, /* free_id = */ TRUE);
        free(new_port);
        return INVALID_FIELD(response_json, "type");
    }
    char *type = json_str_get(child);

    if (!strcmp(type, "number")) {
        new_port->type = PORT_TYPE_NUMBER;
    }
    else if (!strcmp(type, "boolean")) {
        new_port->type = PORT_TYPE_BOOLEAN;
    }
    else {
        port_cleanup(new_port, /* free_id = */ TRUE);
        free(new_port);
        return INVALID_FIELD(response_json, "type");
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
            port_cleanup(new_port, /* free_id = */ TRUE);
            free(new_port);
            return INVALID_FIELD(response_json, "min");
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
            port_cleanup(new_port, /* free_id = */ TRUE);
            free(new_port);
            return INVALID_FIELD(response_json, "max");
        }

        /* min must be <= max */
        if (!IS_UNDEFINED(new_port->min) && new_port->min > new_port->max) {
            port_cleanup(new_port, /* free_id = */ TRUE);
            free(new_port);
            return INVALID_FIELD(response_json, "max");
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
            port_cleanup(new_port, /* free_id = */ TRUE);
            free(new_port);
            return INVALID_FIELD(response_json, "integer");
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
            port_cleanup(new_port, /* free_id = */ TRUE);
            free(new_port);
            return INVALID_FIELD(response_json, "step");
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
            port_cleanup(new_port, /* free_id = */ TRUE);
            free(new_port);
            return INVALID_FIELD(response_json, "choices");
        }

        int len = json_list_get_len(child);
        if (len < 1 || len > 256) {
            port_cleanup(new_port, /* free_id = */ TRUE);
            free(new_port);
            return INVALID_FIELD(response_json, "choices");
        }

        new_port->choices = zalloc((json_list_get_len(child) + 1) * sizeof(char *));
        for (i = 0; i < len; i++) {
            c = json_list_value_at(child, i);
            if (json_get_type(c) != JSON_TYPE_OBJ) {
                port_cleanup(new_port, /* free_id = */ TRUE);
                free(new_port);
                return INVALID_FIELD(response_json, "choices");
            }

            /* value */
            c2 = json_obj_lookup_key(c, "value");
            if (!c2) {
                port_cleanup(new_port, /* free_id = */ TRUE);
                free(new_port);
                return INVALID_FIELD(response_json, "choices");
            }

            if (json_get_type(c2) == JSON_TYPE_INT) {
                new_port->choices[i] = strdup(dtostr(json_int_get(c2), /* decimals = */ 0));
            }
            else if (json_get_type(c2) == JSON_TYPE_DOUBLE) {
                new_port->choices[i] = strdup(dtostr(json_double_get(c2), /* decimals = */ -1));
            }
            else {
                port_cleanup(new_port, /* free_id = */ TRUE);
                free(new_port);
                return INVALID_FIELD(response_json, "choices");
            }

            /* display_name */
            c2 = json_obj_lookup_key(c, "display_name");
            if (c2) {
                if (json_get_type(c2) != JSON_TYPE_STR) {
                    port_cleanup(new_port, /* free_id = */ TRUE);
                    free(new_port);
                    return INVALID_FIELD(response_json, "choices");
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
        port_cleanup(new_port, /* free_id = */ TRUE);
        free(new_port);
        return API_ERROR(response_json, 500, "port-register-error");
    }

    port_register(new_port);

    /* Newly added ports must be automatically enabled */
    new_port->flags |= PORT_FLAG_ENABLED;

    /* Rebuild deps mask for all ports, as the new port might be among their deps */
    ports_rebuild_change_dep_mask();

    config_mark_for_saving();
    event_push_port_add(new_port);

    *code = 201;

    json_refs_ctx_t json_refs_ctx;
    json_refs_ctx_init(&json_refs_ctx, JSON_REFS_TYPE_PORT);

    json_free(response_json);

    return port_to_json(new_port, &json_refs_ctx);
}

json_t *api_put_ports(json_t *query_json, json_t *request_json, int *code) {
    json_t *response_json = json_obj_new();

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_ADMIN);
    }

    DEBUG_API("restoring ports");

    /* Remember port IDs associated to each peripheral; we need them later for recreation */
    uint8 i, j;
    char **port_ids_by_peripheral_index[PERIPHERAL_MAX_NUM];
    uint8 port_ids_len_by_peripheral_index[PERIPHERAL_MAX_NUM];
    for (i = 0; i < PERIPHERAL_MAX_NUM; i++) {
        port_ids_by_peripheral_index[i] = NULL;
        port_ids_len_by_peripheral_index[i] = 0;
    }

    /* Remove all ports */
    port_t *port;
    while (all_ports_count > 0) {
        port = all_ports[0];
        DEBUG_API("removing port %s", port->id);

        if (port->peripheral) { /* Virtual ports don't have a peripheral */
            uint8 peripheral_index = port->peripheral->index;
            uint8 port_ids_len = ++(port_ids_len_by_peripheral_index[peripheral_index]);
            char **port_ids = port_ids_by_peripheral_index[peripheral_index] = realloc(
                port_ids_by_peripheral_index[peripheral_index],
                sizeof(char *) * port_ids_len
            );

            port_ids[port_ids_len - 1] = strdup(port->id);
        }

        port_cleanup(port, /* free_id = */ FALSE);
        if (IS_PORT_VIRTUAL(port)) {
            virtual_port_unregister(port);
        }

        port_unregister(port);
        free(port);
    }

    /* Recreate peripheral ports */
    peripheral_t *peripheral;
    for (i = 0; i < all_peripherals_count; i++) {
        peripheral = all_peripherals[i];
        peripheral_cleanup(peripheral);
        peripheral_init(peripheral);
        peripheral_make_ports(peripheral, port_ids_by_peripheral_index[i], port_ids_len_by_peripheral_index[i]);
    }

    /* Free remembered port IDs */
    for (i = 0; i < PERIPHERAL_MAX_NUM; i++) {
        uint8 port_ids_len = port_ids_len_by_peripheral_index[i];
        for (j = 0; j < port_ids_len; j++) {
            free(port_ids_by_peripheral_index[i][j]);
        }

        free(port_ids_by_peripheral_index[i]);
    }

    /* Actually apply supplied ports configuration */
    json_t *error_response_json = NULL;
    char *error_port_id;
    if (config_apply_ports_provisioning(request_json, &error_response_json, &error_port_id)) {
        *code = 204;
    }
    else {
        DEBUG_API("provisioning ports failed");
        json_free(response_json);
        response_json = error_response_json;
        if (error_port_id) {
            json_obj_append(response_json, "id", json_str_new(error_port_id));
            free(error_port_id);
        }
        *code = 400;
    }

    /* Save, but only if everything went well */
    if (*code == 204) {
        config_mark_for_saving();
    }

    event_push_full_update();

    return response_json;
}

json_t *api_patch_port(port_t *port, json_t *query_json, json_t *request_json, int *code, bool provisioning) {
    json_t *response_json = json_obj_new();

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_ADMIN);
    }

    DEBUG_API("updating attributes of port %s", port->id);

    if (json_get_type(request_json) != JSON_TYPE_OBJ) {
        return API_ERROR(response_json, 400, "invalid-request");
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
            return INVALID_FIELD(response_json, key);
        }

        if (json_bool_get(child) && !IS_PORT_ENABLED(port)) {
            port_enable(port);
        }
        else if (!json_bool_get(child) && IS_PORT_ENABLED(port)) {
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
                if (!IS_ATTRDEF_MODIFIABLE(a)) {
                    json_free(child);
                    if (provisioning) {
                        continue;
                    }
                    else {
                        return ATTR_NOT_MODIFIABLE(response_json, key);
                    }
                }

                switch (a->type) {
                    case ATTR_TYPE_BOOLEAN: {
                        if (json_get_type(child) != JSON_TYPE_BOOL) {
                            json_free(child);
                            return INVALID_FIELD(response_json, key);
                        }

                        bool value = json_bool_get(child);

                        ((int_setter_t) a->set)(port, a, value);

                        DEBUG_PORT(port, "%s set to %d", a->name, value);

                        break;
                    }

                    case ATTR_TYPE_NUMBER: {
                        if (json_get_type(child) != JSON_TYPE_INT &&
                            (json_get_type(child) != JSON_TYPE_DOUBLE || IS_ATTRDEF_INTEGER(a))) {

                            json_free(child);
                            return INVALID_FIELD(response_json, key);
                        }

                        double value = json_get_type(child) == JSON_TYPE_INT ?
                                       json_int_get(child) : json_double_get(child);
                        int idx = validate_num(value, a->min, a->max, IS_ATTRDEF_INTEGER(a), a->step, a->choices);
                        if (!idx) {
                            json_free(child);
                            return INVALID_FIELD(response_json, key);
                        }

                        if (a->choices) {
                            ((int_setter_t) a->set)(port, a, idx - 1);
                        }
                        else {
                            if (IS_ATTRDEF_INTEGER(a)) {
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
                            return INVALID_FIELD(response_json, key);
                        }

                        char *value = json_str_get(child);
                        int idx = validate_str(value, a->choices);
                        if (!idx) {
                            json_free(child);
                            return INVALID_FIELD(response_json, key);
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
                return INVALID_FIELD(response_json, key);
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
                return INVALID_FIELD(response_json, key);
            }

            free(port->unit);
            port->unit = NULL;
            if (*json_str_get(child)) {
                port->unit = strndup(json_str_get(child), PORT_MAX_UNIT_LEN);
            }

            DEBUG_PORT(port, "unit set to \"%s\"", port->unit ? port->unit : "");
        }
        else if (IS_PORT_WRITABLE(port) && !strcmp(key, "expression")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(response_json, key);
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
                    return INVALID_EXPRESSION(
                        response_json,
                        "expression",
                        "too-long",
                        /* token = */ NULL,
                        /* pos = */ -1
                    );
                }

                expr_t *expr = expr_parse(port->id, sexpr, strlen(sexpr));
                if (!expr) {
                    return INVALID_EXPRESSION_FROM_ERROR(response_json, "expression");
                }

                if (expr_check_loops(expr, port) > 1) {
                    DEBUG_API("loop detected in expression \"%s\"", sexpr);
                    expr_free(expr);
                    return INVALID_EXPRESSION(
                        response_json,
                        "expression",
                        "circular-dependency",
                        /* token = */ NULL,
                        /* pos = */ -1
                    );
                }

                port->sexpr = strdup(sexpr);

                if (IS_PORT_ENABLED(port)) {
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
        else if (IS_PORT_WRITABLE(port) && !strcmp(key, "transform_write")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(response_json, key);
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
                    return INVALID_EXPRESSION(
                        response_json,
                        "transform_write",
                        "too-long",
                        /* token = */ NULL,
                        /* pos = */ -1
                    );
                }

                expr_t *transform_write = expr_parse(port->id, stransform_write, strlen(stransform_write));
                if (!transform_write) {
                    return INVALID_EXPRESSION_FROM_ERROR(response_json, "transform_write");
                }

                port->stransform_write = strdup(stransform_write);
                port->transform_write = transform_write;

                DEBUG_PORT(port, "write transform set to \"%s\"", port->stransform_write ? port->stransform_write : "");
            }
        }
        else if (!strcmp(key, "transform_read")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(response_json, key);
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
                    return INVALID_EXPRESSION(
                        response_json,
                        "transform_read",
                        "too-long",
                        /* token = */ NULL,
                        /* pos = */ -1
                    );
                }

                expr_t *transform_read = expr_parse(port->id, stransform_read, strlen(stransform_read));
                if (!transform_read) {
                    return INVALID_EXPRESSION_FROM_ERROR(response_json, "transform_read");
                }

                port->stransform_read = strdup(stransform_read);
                port->transform_read = transform_read;

                DEBUG_PORT(port, "read transform set to \"%s\"", port->stransform_read ? port->stransform_read : "");
            }
        }
        else if (!strcmp(key, "persisted")) {
            if (json_get_type(child) != JSON_TYPE_BOOL) {
                return INVALID_FIELD(response_json, key);
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
                return INVALID_FIELD(response_json, key);
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
        else if (!IS_PORT_VIRTUAL(port) && !strcmp(key, "sampling_interval")) {
            if (json_get_type(child) != JSON_TYPE_INT) {
                return INVALID_FIELD(response_json, key);
            }

            int sampling_interval = json_int_get(child);
            bool valid = validate_num(
                sampling_interval,
                port->min_sampling_interval,
                port->max_sampling_interval,
                TRUE,
                0,
                NULL
            );
            if (!valid) {
                return INVALID_FIELD(response_json, key);
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

            if (!provisioning) {
                return ATTR_NOT_MODIFIABLE(response_json, key);
            }
        }
        else {
            if (!provisioning) {
                return NO_SUCH_ATTR(response_json, key);
            }
        }
    }
    
    port_configure(port);

    /* Write the value to port; this allows using persistent value when enabling a port later */
    if (IS_PORT_ENABLED(port) && IS_PORT_WRITABLE(port) && !IS_UNDEFINED(port->last_read_value)) {
        port_write_value(port, port->last_read_value, CHANGE_REASON_NATIVE);
    }

    config_mark_for_saving();
    event_push_port_update(port);

    *code = 204;

    return response_json;
}

json_t *api_delete_port(port_t *port, json_t *query_json, int *code) {
    DEBUG_API("deleting virtual port %s", port->id);

    json_t *response_json = json_obj_new();

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_ADMIN);
    }

    if (!IS_PORT_VIRTUAL(port)) {
        return API_ERROR(response_json, 400, "port-not-removable");  /* Can't unregister a non-virtual port */
    }

    event_push_port_remove(port);

    port_cleanup(port, /* free_id = */ FALSE);

    if (!virtual_port_unregister(port)) {
        return API_ERROR(response_json, 500, "port-unregister-error");
    }

    if (!port_unregister(port)) {
        return API_ERROR(response_json, 500, "port-unregister-error");
    }

    config_mark_for_saving();

    free(port);

    /* Rebuild deps mask for all ports, as the former port might have been among their deps */
    ports_rebuild_change_dep_mask();

    *code = 204;

    return response_json;
}

json_t *api_get_port_value(port_t *port, json_t *query_json, int *code) {
    json_t *response_json = NULL;

    if (api_access_level < API_ACCESS_LEVEL_VIEWONLY) {
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_VIEWONLY);
    }

    /* Poll ports before retrieving current port value, ensuring value is as up-to-date as possible */
    core_poll();

    response_json = port_make_json_value(port);

    *code = 200;

    return response_json;
}

json_t *api_patch_port_value(port_t *port, json_t *query_json, json_t *request_json, int *code) {
    json_t *response_json = json_obj_new();

    if (api_access_level < API_ACCESS_LEVEL_NORMAL) {
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_NORMAL);
    }

    if (!IS_PORT_ENABLED(port)) {
        return API_ERROR(response_json, 400, "port-disabled");
    }

    if (!IS_PORT_WRITABLE(port)) {
        return API_ERROR(response_json, 400, "read-only-port");
    }

    if (port->type == PORT_TYPE_BOOLEAN) {
        if (json_get_type(request_json) != JSON_TYPE_BOOL) {
            return API_ERROR(response_json, 400, "invalid-value");
        }

        if (!port_write_value(port, json_bool_get(request_json), CHANGE_REASON_API)) {
            return API_ERROR(response_json, 400, "invalid-value");
        }
    }
    else if (port->type == PORT_TYPE_NUMBER) {
        if (json_get_type(request_json) != JSON_TYPE_INT &&
            (json_get_type(request_json) != JSON_TYPE_DOUBLE || port->integer)) {

            return API_ERROR(response_json, 400, "invalid-value");
        }

        double value = (
            json_get_type(request_json) == JSON_TYPE_INT ?
            json_int_get(request_json) :
            json_double_get(request_json)
        );

        if (!validate_num(value, port->min, port->max, port->integer, port->step, port->choices)) {
            return API_ERROR(response_json, 400, "invalid-value");
        }

        if (!port_write_value(port, value, CHANGE_REASON_API)) {
            return API_ERROR(response_json, 400, "invalid-value");
        }
    }

    *code = 204;
    
    return response_json;
}

json_t *api_patch_port_sequence(port_t *port, json_t *query_json, json_t *request_json, int *code) {
    json_t *response_json = NULL;

    if (api_access_level < API_ACCESS_LEVEL_NORMAL) {
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_NORMAL);
    }

    if (!IS_PORT_ENABLED(port)) {
        return API_ERROR(response_json, 400, "port-disabled");
    }

    if (!IS_PORT_WRITABLE(port)) {
        return API_ERROR(response_json, 400, "read-only-port");
    }

    if (port->expr) {
        return API_ERROR(response_json, 400, "port-with-expression");
    }

    json_t *values_json = json_obj_lookup_key(request_json, "values");
    if (!values_json) {
        return MISSING_FIELD(response_json, "values");
    }
    if (json_get_type(values_json) != JSON_TYPE_LIST || !json_list_get_len(values_json)) {
        return INVALID_FIELD(response_json, "values");
    }    

    json_t *delays_json = json_obj_lookup_key(request_json, "delays");
    if (!delays_json) {
        return MISSING_FIELD(response_json, "delays");
    }
    if (json_get_type(delays_json) != JSON_TYPE_LIST || !json_list_get_len(delays_json)) {
        return INVALID_FIELD(response_json, "delays");
    }
    if (json_list_get_len(delays_json) != json_list_get_len(values_json)) {
        return INVALID_FIELD(response_json, "delays");
    }

    json_t *repeat_json = json_obj_lookup_key(request_json, "repeat");
    if (!repeat_json) {
        return MISSING_FIELD(response_json, "repeat");
    }
    if (json_get_type(repeat_json) != JSON_TYPE_INT) {
        return INVALID_FIELD(response_json, "repeat");
    }
    int repeat = json_int_get(repeat_json);
    if (repeat < API_MIN_SEQUENCE_REPEAT || repeat > API_MAX_SEQUENCE_REPEAT) {
        return INVALID_FIELD(response_json, "repeat");
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
                return SEQ_INVALID_FIELD(response_json, "values");
            }

            port->sequence_values[i] = json_bool_get(j);
        }
        if (port->type == PORT_TYPE_NUMBER) {
            if (json_get_type(j) != JSON_TYPE_INT &&
                (json_get_type(j) != JSON_TYPE_DOUBLE || port->integer)) {

                return SEQ_INVALID_FIELD(response_json, "values");
            }

            double value = json_get_type(j) == JSON_TYPE_INT ? json_int_get(j) : json_double_get(j);
            if (!validate_num(value, port->min, port->max, port->integer, port->step, port->choices)) {
                return SEQ_INVALID_FIELD(response_json, "values");
            }

            port->sequence_values[i] = value;
        }
    }

    /* Delays */
    for (i = 0; i < json_list_get_len(delays_json); i++) {
        j = json_list_value_at(delays_json, i);
        if (json_get_type(j) != JSON_TYPE_INT) {
            return SEQ_INVALID_FIELD(response_json, "delays");
        }
        
        port->sequence_delays[i] = json_int_get(j);
        
        if (port->sequence_delays[i] < API_MIN_SEQUENCE_DELAY || port->sequence_delays[i] > API_MAX_SEQUENCE_DELAY) {
            return SEQ_INVALID_FIELD(response_json, "delays");
        }
    }
    
    /* Start sequence timer */
    port->sequence_timer = zalloc(sizeof(os_timer_t));
    os_timer_disarm(port->sequence_timer);
    os_timer_setfn(port->sequence_timer, on_sequence_timer, port);
    os_timer_arm(port->sequence_timer, 1, /* repeat = */ FALSE);

    response_json = json_obj_new();
    *code = 204;
    
    return response_json;
}

json_t *api_get_webhooks(json_t *query_json, int *code) {
    json_t *response_json = json_obj_new();

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_ADMIN);
    }

    DEBUG_API("returning webhooks parameters");

    json_obj_append(response_json, "enabled", json_bool_new(device_flags & DEVICE_FLAG_WEBHOOKS_ENABLED));
    json_obj_append(
        response_json,
        "scheme",
        json_str_new(device_flags & DEVICE_FLAG_WEBHOOKS_HTTPS ? "https" : "http")
    );
    json_obj_append(response_json, "host", json_str_new(webhooks_host ? webhooks_host : ""));
    json_obj_append(response_json, "port", json_int_new(webhooks_port));
    json_obj_append(response_json, "path", json_str_new(webhooks_path ? webhooks_path : ""));
    json_obj_append(response_json, "password_hash", json_str_new(webhooks_password_hash));

    json_t *json_events = json_list_new();

    int e;
    for (e = EVENT_TYPE_MIN; e <= EVENT_TYPE_MAX; e++) {
        if (webhooks_events_mask & BIT(e)) {
            json_list_append(json_events, json_str_new(EVENT_TYPES_STR[e]));
        }
    }

    json_obj_append(response_json, "events", json_events);
    json_obj_append(response_json, "timeout", json_int_new(webhooks_timeout));
    json_obj_append(response_json, "retries", json_int_new(webhooks_retries));

    *code = 200;

    return response_json;
}

json_t *api_put_webhooks(json_t *query_json, json_t *request_json, int *code) {
    DEBUG_API("updating webhooks parameters");

    json_t *response_json = json_obj_new();

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_ADMIN);
    }

    if (json_get_type(request_json) != JSON_TYPE_OBJ) {
        return API_ERROR(response_json, 400, "invalid-request");
    }

    /* Enabled */
    json_t *enabled_json = json_obj_lookup_key(request_json, "enabled");
    if (!enabled_json) {
        return MISSING_FIELD(response_json, "enabled");
    }
    if (json_get_type(enabled_json) != JSON_TYPE_BOOL) {
        return INVALID_FIELD(response_json, "enabled");
    }

    bool enabled = json_bool_get(enabled_json);

    /* Scheme */
    json_t *scheme_json = json_obj_lookup_key(request_json, "scheme");
    if (!scheme_json) {
        return MISSING_FIELD(response_json, "scheme");
    }
    if (json_get_type(scheme_json) != JSON_TYPE_STR) {
        return INVALID_FIELD(response_json, "scheme");
    }

    bool scheme_https = FALSE;
#ifdef _SSL
    if (!strcmp(json_str_get(scheme_json), "https")) {
        scheme_https = TRUE;
    }
    else
#endif
    if (strcmp(json_str_get(scheme_json), "http")) {
        return INVALID_FIELD(response_json, "scheme");
    }

    /* Host */
    json_t *host_json = json_obj_lookup_key(request_json, "host");
    if (!host_json) {
        return MISSING_FIELD(response_json, "host");
    }
    if (json_get_type(host_json) != JSON_TYPE_STR) {
        return INVALID_FIELD(response_json, "host");
    }
    if (strlen(json_str_get(host_json)) == 0 && enabled) {
        return INVALID_FIELD(response_json, "host");
    }

    /* Port */
    json_t *port_json = json_obj_lookup_key(request_json, "port");
    if (!port_json) {
        return MISSING_FIELD(response_json, "port");
    }
    if (json_get_type(port_json) != JSON_TYPE_INT) {
        return INVALID_FIELD(response_json, "port");
    }
    if (json_int_get(port_json) < 1 || json_int_get(port_json) > 65535) {
        return INVALID_FIELD(response_json, "port");
    }

    /* Path */
    json_t *path_json = json_obj_lookup_key(request_json, "path");
    if (!path_json) {
        return MISSING_FIELD(response_json, "path");
    }
    if (json_get_type(path_json) != JSON_TYPE_STR) {
        return INVALID_FIELD(response_json, "path");
    }
    if (strlen(json_str_get(path_json)) == 0 && enabled) {
        return INVALID_FIELD(response_json, "path");
    }

    /* Password */
    json_t *password_json = json_obj_lookup_key(request_json, "password");
    json_t *password_hash_json = json_obj_lookup_key(request_json, "password_hash");
    if (!password_json && !password_hash_json) {
        /* At least one of the two must be supplied */
        return MISSING_FIELD(response_json, "password");
    }
    if (password_json && json_get_type(password_json) != JSON_TYPE_STR) {
        return INVALID_FIELD(response_json, "password");
    }
    if (password_hash_json) {
        if (json_get_type(password_hash_json) != JSON_TYPE_STR) {
            return INVALID_FIELD(response_json, "password_hash");
        }
        char *password_hash = json_str_get(password_hash_json);
        if (strlen(password_hash) != SHA256_HEX_LEN) {
            return INVALID_FIELD(response_json, "password_hash");
        }
    }

    /* Events */
    json_t *event_json, *events_json = json_obj_lookup_key(request_json, "events");
    if (!events_json) {
        return MISSING_FIELD(response_json, "events");
    }
    if (json_get_type(events_json) != JSON_TYPE_LIST) {
        return INVALID_FIELD(response_json, "events");
    }

    int i, e, len = json_list_get_len(events_json);
    uint8 events_mask = 0;
    for (i = 0; i < len; i++) {
        event_json = json_list_value_at(events_json, i);
        if (json_get_type(event_json) != JSON_TYPE_STR) {
            return INVALID_FIELD(response_json, "events");
        }

        for (e = EVENT_TYPE_MIN; e <= EVENT_TYPE_MAX; e++) {
            if (!strcmp(json_str_get(event_json), EVENT_TYPES_STR[e])) {
                events_mask |= BIT(e);
                break;
            }
        }

        if (e > EVENT_TYPE_MAX) {
            return INVALID_FIELD(response_json, "events");
        }

        DEBUG_WEBHOOKS("event mask includes %s", EVENT_TYPES_STR[e]);
    }

    /* Timeout */
    json_t *timeout_json = json_obj_lookup_key(request_json, "timeout");
    if (!timeout_json) {
        return MISSING_FIELD(response_json, "timeout");
    }
    if (json_get_type(timeout_json) != JSON_TYPE_INT) {
        return INVALID_FIELD(response_json, "timeout");
    }
    if (json_int_get(timeout_json) < WEBHOOKS_MIN_TIMEOUT || json_int_get(timeout_json) > WEBHOOKS_MAX_TIMEOUT) {
        return INVALID_FIELD(response_json, "timeout");
    }

    /* Retries */
    json_t *retries_json = json_obj_lookup_key(request_json, "retries");
    if (!retries_json) {
        return MISSING_FIELD(response_json, "retries");
    }
    if (json_get_type(retries_json) != JSON_TYPE_INT) {
        return INVALID_FIELD(response_json, "retries");
    }
    if (json_int_get(retries_json) < WEBHOOKS_MIN_RETRIES || json_int_get(retries_json) > WEBHOOKS_MAX_RETRIES) {
        return INVALID_FIELD(response_json, "retries");
    }

    /* Now that we've validated input data, we can apply changes */
    if (enabled) {
        device_flags |= DEVICE_FLAG_WEBHOOKS_ENABLED;
        DEBUG_WEBHOOKS("enabled");
    }
    else {
        device_flags &= ~DEVICE_FLAG_WEBHOOKS_ENABLED;
        DEBUG_WEBHOOKS("disabled");
    }

    if (scheme_https) {
        device_flags |= DEVICE_FLAG_WEBHOOKS_HTTPS;
        DEBUG_WEBHOOKS("scheme set to HTTPS");
    }
    else {
        DEBUG_WEBHOOKS("scheme set to HTTP");
        device_flags &= ~DEVICE_FLAG_WEBHOOKS_HTTPS;
    }

    free(webhooks_host);
    webhooks_host = strdup(json_str_get(host_json));
    DEBUG_WEBHOOKS("host set to \"%s\"", webhooks_host);

    webhooks_port = json_int_get(port_json);
    DEBUG_WEBHOOKS("port set to %d", webhooks_port);

    free(webhooks_path);
    webhooks_path = strdup(json_str_get(path_json));
    DEBUG_WEBHOOKS("path set to \"%s\"", webhooks_path);

    char *password_hash;
    if (password_json) {
        char *password = json_str_get(password_json);
        password_hash = sha256_hex(password);
    }
    else { /* Assuming password_hash supplied */
        password_hash = strdup(json_str_get(password_hash_json));
    }
    strcpy(webhooks_password_hash, password_hash);
    free(password_hash);
    DEBUG_WEBHOOKS("password set");

    webhooks_events_mask = events_mask;

    webhooks_timeout = json_int_get(timeout_json);
    DEBUG_WEBHOOKS("timeout set to %d", webhooks_timeout);

    webhooks_retries = json_int_get(retries_json);
    DEBUG_WEBHOOKS("retries set to %d", webhooks_retries);

    config_mark_for_saving();

    *code = 204;

    return response_json;
}

json_t *api_get_wifi(json_t *query_json, int *code) {
    json_t *response_json = NULL;

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_ADMIN);
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
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_ADMIN);
    }

    DEBUG_API("returning raw value for %s", io);

    if (!strncmp(io, "gpio", 4)) {
        int gpio_no = strtol(io + 4, NULL, 10);
        if (gpio_no < 0 || gpio_no > 16) {
            return API_ERROR(response_json, 404, "no-such-io");
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
        return API_ERROR(response_json, 404, "no-such-io");
    }

    *code = 200;

    return response_json;
}

json_t *api_patch_raw_io(char *io, json_t *query_json, json_t *request_json, int *code) {
    json_t *response_json = json_obj_new();
    json_t *value_json, *param_json;
    uint32 len, i;

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_ADMIN);
    }

    if (json_get_type(request_json) != JSON_TYPE_OBJ) {
        return API_ERROR(response_json, 400, "invalid-request");
    }

    if (!strncmp(io, "gpio", 4)) {
        int gpio_no = strtol(io + 4, NULL, 10);
        if (gpio_no < 0 || gpio_no > 16) {
            return API_ERROR(response_json, 404, "no-such-io");
        }

        json_t *value_json = json_obj_lookup_key(request_json, "value");
        bool value = gpio_read_value(gpio_no);
        if (value_json) {
            if (json_get_type(value_json) != JSON_TYPE_BOOL) {
                return INVALID_FIELD(response_json, "value");
            }

            value = json_bool_get(value_json);
        }

        json_t *output_json = json_obj_lookup_key(request_json, "output");
        bool output = gpio_is_output(gpio_no);
        if (output_json) {
            if (json_get_type(output_json) != JSON_TYPE_BOOL) {
                return INVALID_FIELD(response_json, "output");
            }

            output = json_bool_get(output_json);
        }

        json_t *pull_up_json = json_obj_lookup_key(request_json, "pull_up");
        bool pull_up = gpio_get_pull(gpio_no);
        if (pull_up_json) {
            if (json_get_type(pull_up_json) != JSON_TYPE_BOOL) {
                return INVALID_FIELD(response_json, "pull_up");
            }

            pull_up = json_bool_get(pull_up_json);
        }

        json_t *pull_down_json = json_obj_lookup_key(request_json, "pull_down");
        bool pull_down = !gpio_get_pull(gpio_no);
        if (pull_down_json) {
            if (json_get_type(pull_down_json) != JSON_TYPE_BOOL) {
                return INVALID_FIELD(response_json, "pull_down");
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
        return API_ERROR(response_json, 400, "read-only-io");
    }
    else if (!strcmp(io, "hspi")) {
        value_json = json_obj_lookup_key(request_json, "value");
        if (value_json) {
            if (json_get_type(value_json) != JSON_TYPE_LIST) {
                return INVALID_FIELD(response_json, "value");
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
                    return INVALID_FIELD(response_json, "value");
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
                return INVALID_FIELD(response_json, "freq");
            }

            freq = json_int_get(param_json);
            reconfigured = TRUE;
        }

        param_json = json_obj_lookup_key(request_json, "cpol");
        if (param_json) {
            if (json_get_type(param_json) != JSON_TYPE_BOOL) {
                return INVALID_FIELD(response_json, "cpol");
            }

            cpol = json_bool_get(param_json);
            reconfigured = TRUE;
        }

        param_json = json_obj_lookup_key(request_json, "cpha");
        if (param_json) {
            if (json_get_type(param_json) != JSON_TYPE_BOOL) {
                return INVALID_FIELD(response_json, "cpha");
            }

            cpha = json_bool_get(param_json);
            reconfigured = TRUE;
        }

        param_json = json_obj_lookup_key(request_json, "bit_order");
        if (param_json) {
            if (json_get_type(param_json) != JSON_TYPE_STR) {
                return INVALID_FIELD(response_json, "bit_order");
            }

            bit_order = !strcmp(json_str_get(param_json), "lsb_first");
            reconfigured = TRUE;
        }

        if (reconfigured) {
            hspi_setup(bit_order, cpol, cpha, freq);
        }
    }
    else {
        return API_ERROR(response_json, 404, "no-such-io");
    }

    *code = 204;

    return response_json;
}

json_t *api_get_peripherals(json_t *query_json, int *code) {
    json_t *response_json = json_list_new();

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_ADMIN);
    }

    for (int i = 0; i < all_peripherals_count; i++) {
        peripheral_t *peripheral = all_peripherals[i];
        json_list_append(response_json, peripheral_to_json(peripheral));
    }

    *code = 200;

    return response_json;
}

json_t *api_put_peripherals(json_t *query_json, json_t *request_json, int *code) {
    json_t *response_json = json_obj_new();
    json_t *peripheral_config;
    json_t *type_json;
    json_t *flags_json;
    json_t *params_json;
    json_t *port_ids_json;
    json_t *json;

    uint16 type_id;
    uint16 flags;
    char **port_ids = NULL;
    int i, index;
    uint8 port_ids_len = 0;
    peripheral_t *peripheral;

    uint8 int8_params[PERIPHERAL_MAX_INT8_PARAMS];
    uint8 int8_param;
    uint8 int8_param_count;

    uint16 int16_params[PERIPHERAL_MAX_INT16_PARAMS];
    uint16 int16_param;
    uint8 int16_param_count;

    uint32 int32_params[PERIPHERAL_MAX_INT32_PARAMS];
    uint32 int32_param;
    uint8 int32_param_count;

    uint64 int64_params[PERIPHERAL_MAX_INT64_PARAMS];
    uint64 int64_param;
    uint8 int64_param_count;

    double double_params[PERIPHERAL_MAX_DOUBLE_PARAMS];
    double double_param;
    uint8 double_param_count;

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_ADMIN);
    }

    if (json_get_type(request_json) != JSON_TYPE_LIST) {
        return API_ERROR(response_json, 400, "invalid-request");
    }

    if (json_list_get_len(request_json) > PERIPHERAL_MAX_NUM) {
        return API_ERROR(response_json, 400, "too-many-peripherals");
    }

    /* Unregister all (non-virtual) ports */
    port_t *p;
    for (i = 0; i < all_ports_count; i++) {
        p = all_ports[i];
        if (IS_PORT_VIRTUAL(p)) {
            continue;
        }

        port_cleanup(p, /* free_id = */ FALSE);

        if (!port_unregister(p)) {
            return API_ERROR(response_json, 500, "port-unregister-error");
        }

        free(p);

        i--; /* Current port was removed from all_ports; position i should be processed again */
    }

    /* Rebuild deps mask for remaining (virtual) ports */
    ports_rebuild_change_dep_mask();

    while (all_peripherals_count) {
        peripheral = all_peripherals[0];
        peripheral_unregister(peripheral);
        peripheral_cleanup(peripheral);
        peripheral_free(peripheral);
    }

    DEBUG_PERIPHERALS("creating newly supplied peripherals");

    for (index = 0; index < json_list_get_len(request_json); index++) {
        peripheral_config = json_list_value_at(request_json, index);

        flags = 0;
        int8_param_count = 0;
        int16_param_count = 0;
        int32_param_count = 0;
        int64_param_count = 0;
        double_param_count = 0;
        port_ids_len = 0;

        if (json_get_type(peripheral_config) != JSON_TYPE_OBJ) {
            return API_ERROR(response_json, 400, "invalid-request");
        }

        /* Type */
        type_json = json_obj_lookup_key(peripheral_config, "type");
        if (!type_json || json_get_type(type_json) != JSON_TYPE_INT) {
            return INVALID_FIELD(response_json, "type");
        }
        type_id = json_int_get(type_json);
        if (type_id > PERIPHERAL_MAX_TYPE_ID || type_id < 1) {
            return INVALID_FIELD(response_json, "type");
        }

        /* Flags */
        flags_json = json_obj_lookup_key(peripheral_config, "flags");
        if (flags_json) {
            if (json_get_type(flags_json) == JSON_TYPE_STR) {
                flags = strtol(json_str_get(flags_json), NULL, 2);
            }
            else if (json_get_type(flags_json) == JSON_TYPE_INT) {
                flags = json_int_get(flags_json);
            }
            else {
                return INVALID_FIELD(response_json, "flags");
            }
        }

        /* int8 params */
        params_json = json_obj_lookup_key(peripheral_config, "int8_params");
        if (params_json) {
            if ((json_get_type(params_json) != JSON_TYPE_LIST) ||
                (json_list_get_len(params_json) > PERIPHERAL_MAX_INT8_PARAMS)) {

                return INVALID_FIELD(response_json, "int8_params");
            }

            for (i = 0; i < json_list_get_len(params_json); i++) {
                json = json_list_value_at(params_json, i);
                if (json_get_type(json) == JSON_TYPE_STR) { /* Hex value */
                    int8_param = strtol(json_str_get(json), NULL, 16);
                }
                else if (json_get_type(json) == JSON_TYPE_INT) {
                    int8_param = json_int_get(json);
                }
                else {
                    return INVALID_FIELD(response_json, "int8_params");
                }

                int8_params[int8_param_count++] = int8_param;
            }
        }

        /* int16 params */
        params_json = json_obj_lookup_key(peripheral_config, "int16_params");
        if (params_json) {
            if ((json_get_type(params_json) != JSON_TYPE_LIST) ||
                (json_list_get_len(params_json) > PERIPHERAL_MAX_INT16_PARAMS)) {

                return INVALID_FIELD(response_json, "int16_params");
            }

            for (i = 0; i < json_list_get_len(params_json); i++) {
                json = json_list_value_at(params_json, i);
                if (json_get_type(json) == JSON_TYPE_STR) { /* Hex value */
                    int16_param = strtol(json_str_get(json), NULL, 16);
                }
                else if (json_get_type(json) == JSON_TYPE_INT) {
                    int16_param = json_int_get(json);
                }
                else {
                    return INVALID_FIELD(response_json, "int16_params");
                }

                int16_params[int16_param_count++] = int16_param;
            }
        }

        /* int32 params */
        params_json = json_obj_lookup_key(peripheral_config, "int32_params");
        if (params_json) {
            if ((json_get_type(params_json) != JSON_TYPE_LIST) ||
                (json_list_get_len(params_json) > PERIPHERAL_MAX_INT32_PARAMS)) {

                return INVALID_FIELD(response_json, "int32_params");
            }

            for (i = 0; i < json_list_get_len(params_json); i++) {
                json = json_list_value_at(params_json, i);
                if (json_get_type(json) == JSON_TYPE_STR) { /* Hex value */
                    int32_param = strtol(json_str_get(json), NULL, 16);
                }
                else if (json_get_type(json) == JSON_TYPE_INT) {
                    int32_param = json_int_get(json);
                }
                else {
                    return INVALID_FIELD(response_json, "int32_params");
                }

                int32_params[int32_param_count++] = int32_param;
            }
        }

        /* int64 params */
        params_json = json_obj_lookup_key(peripheral_config, "int64_params");
        if (params_json) {
            if ((json_get_type(params_json) != JSON_TYPE_LIST) ||
                (json_list_get_len(params_json) > PERIPHERAL_MAX_INT64_PARAMS)) {

                return INVALID_FIELD(response_json, "int64_params");
            }

            for (i = 0; i < json_list_get_len(params_json); i++) {
                json = json_list_value_at(params_json, i);
                if (json_get_type(json) == JSON_TYPE_STR) { /* Hex value */
                    int64_param = strtol(json_str_get(json), NULL, 16);
                }
                else if (json_get_type(json) == JSON_TYPE_INT) {
                    int64_param = json_int_get(json);
                }
                else {
                    return INVALID_FIELD(response_json, "int64_params");
                }

                int64_params[int64_param_count++] = int64_param;
            }
        }

        /* double params */
        params_json = json_obj_lookup_key(peripheral_config, "double_params");
        if (params_json) {
            if ((json_get_type(params_json) != JSON_TYPE_LIST) ||
                (json_list_get_len(params_json) > PERIPHERAL_MAX_DOUBLE_PARAMS)) {

                return INVALID_FIELD(response_json, "double_params");
            }

            for (i = 0; i < json_list_get_len(params_json); i++) {
                json = json_list_value_at(params_json, i);
                if (json_get_type(json) == JSON_TYPE_INT) {
                    double_param = json_int_get(json);
                }
                else if (json_get_type(json) == JSON_TYPE_DOUBLE) {
                    double_param = json_double_get(json);
                }
                else {
                    return INVALID_FIELD(response_json, "double_params");
                }

                double_params[double_param_count++] = double_param;
            }
        }

        /* Port IDs */
        port_ids_json = json_obj_lookup_key(peripheral_config, "port_ids");
        if (port_ids_json) {
            if (json_get_type(port_ids_json) != JSON_TYPE_LIST) {
                return INVALID_FIELD(response_json, "port_ids");
            }

            port_ids_len = json_list_get_len(port_ids_json);
            port_ids = malloc(port_ids_len * sizeof(char *));
            for (i = 0; i < json_list_get_len(port_ids_json); i++) {
                json = json_list_value_at(port_ids_json, i);
                if (json_get_type(json) != JSON_TYPE_STR) {
                    free(port_ids);
                    return INVALID_FIELD(response_json, "port_ids");
                }

                port_ids[i] = json_str_get(json);
            }
        }

        /* Create the peripheral & its ports */
        peripheral = zalloc(sizeof(peripheral_t));
        peripheral->index = index;

        peripheral_register(peripheral);

        peripheral->type_id = type_id;
        peripheral->flags = flags;
        peripheral->params = zalloc(PERIPHERAL_PARAMS_SIZE);

        if (int8_param_count) {
            memcpy(peripheral->params + PERIPHERAL_CONFIG_OFFS_INT8_PARAMS, int8_params, int8_param_count);
        }
        if (int16_param_count) {
            memcpy(peripheral->params + PERIPHERAL_CONFIG_OFFS_INT16_PARAMS, int16_params, int16_param_count * 2);
        }
        if (int32_param_count) {
            memcpy(peripheral->params + PERIPHERAL_CONFIG_OFFS_INT32_PARAMS, int32_params, int32_param_count * 4);
        }
        if (int64_param_count) {
            memcpy(peripheral->params + PERIPHERAL_CONFIG_OFFS_INT64_PARAMS, int64_params, int64_param_count * 4);
        }
        if (double_param_count) {
            memcpy(peripheral->params + PERIPHERAL_CONFIG_OFFS_DOUBLE_PARAMS, double_params, double_param_count * 4);
        }

        peripheral_init(peripheral);
        peripheral_make_ports(peripheral, port_ids, port_ids_len);

        free(port_ids);
        port_ids = NULL;
    }

    /* Clear existing configuration name & version, but only when called directly, i.e. not during provisioning */
    if (!config_is_provisioning()) {
        DEBUG_DEVICE("clearing config name");
        device_config_name[0] = '\0';
        device_provisioning_version = 0;
        event_push_full_update();
    }

    config_mark_for_saving();

    *code = 204;

    return response_json;
}

json_t *api_get_system(json_t *query_json, int *code) {
    json_t *response_json = json_obj_new();

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_ADMIN);
    }

    /* Setup button */
    int8 pin;
    bool level;
    uint8 hold, reset_hold;
    system_setup_button_get_config(&pin, &level, &hold, &reset_hold);
    json_t *setup_button_json = json_obj_new();
    json_obj_append(response_json, "setup_button", setup_button_json);
    json_obj_append(setup_button_json, "pin", json_int_new(pin));
    json_obj_append(setup_button_json, "level", json_bool_new(level));
    json_obj_append(setup_button_json, "hold", json_int_new(hold));
    json_obj_append(setup_button_json, "reset_hold", json_int_new(reset_hold));

    /* Status LED */
    system_status_led_get_config(&pin, &level);
    json_t *status_led_json = json_obj_new();
    json_obj_append(response_json, "status_led", status_led_json);
    json_obj_append(status_led_json, "pin", json_int_new(pin));
    json_obj_append(status_led_json, "level", json_bool_new(level));

    /* Battery */
#ifdef _BATTERY
    uint16 div_factor;
    uint16 voltages[BATTERY_LUT_LEN];
    battery_get_config(&div_factor, voltages);
    json_t *battery_json = json_obj_new();
    json_obj_append(response_json, "battery", battery_json);
    json_obj_append(battery_json, "div", json_int_new(div_factor));
    json_t *voltages_json = json_list_new();
    json_obj_append(battery_json, "voltages", voltages_json);
    for (int i = 0; i < BATTERY_LUT_LEN; i++) {
        json_list_append(voltages_json, json_int_new(voltages[i]));
    }
#endif

    *code = 200;

    return response_json;
}

json_t *api_patch_system(json_t *query_json, json_t *request_json, int *code) {
    json_t *response_json = json_obj_new();
    json_t *setup_button_json;
    json_t *status_led_json;
    json_t *json;

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_ADMIN);
    }

    if (json_get_type(request_json) != JSON_TYPE_OBJ) {
        return API_ERROR(response_json, 400, "invalid-request");
    }

    /* Setup button */
    setup_button_json = json_obj_lookup_key(request_json, "setup_button");
    int8 pin = -1;
    bool level = FALSE;
    uint8 hold = 0;
    uint8 reset_hold = 0;
    if (setup_button_json) {
        if (json_get_type(setup_button_json) != JSON_TYPE_OBJ) {
            return INVALID_FIELD(response_json, "setup_button");
        }

        json = json_obj_lookup_key(setup_button_json, "pin");
        if (!json || json_get_type(json) != JSON_TYPE_INT) {
            return INVALID_FIELD(response_json, "setup_button");
        }
        pin = json_int_get(json);
        if (pin > 16) {
            pin = -1;
        }

        json = json_obj_lookup_key(setup_button_json, "level");
        if (!json || json_get_type(json) != JSON_TYPE_BOOL) {
            return INVALID_FIELD(response_json, "setup_button");
        }
        level = json_bool_get(json);

        json = json_obj_lookup_key(setup_button_json, "hold");
        if (!json || json_get_type(json) != JSON_TYPE_INT) {
            return INVALID_FIELD(response_json, "setup_button");
        }
        hold = json_int_get(json);

        json = json_obj_lookup_key(setup_button_json, "reset_hold");
        if (!json || json_get_type(json) != JSON_TYPE_INT) {
            return INVALID_FIELD(response_json, "setup_button");
        }
        reset_hold = json_int_get(json);

    }

    system_setup_button_set_config(pin, level, hold, reset_hold);

    /* Status LED */
    status_led_json = json_obj_lookup_key(request_json, "status_led");
    if (status_led_json) {
        if (json_get_type(status_led_json) != JSON_TYPE_OBJ) {
            return INVALID_FIELD(response_json, "status_led");
        }

        json = json_obj_lookup_key(status_led_json, "pin");
        if (!json || json_get_type(json) != JSON_TYPE_INT) {
            return INVALID_FIELD(response_json, "status_led");
        }
        pin = json_int_get(json);
        if (pin > 16) {
            pin = -1;
        }

        json = json_obj_lookup_key(status_led_json, "level");
        if (!json || json_get_type(json) != JSON_TYPE_BOOL) {
            return INVALID_FIELD(response_json, "status_led");
        }
        level = json_bool_get(json);
    }

    system_status_led_set_config(pin, level);

    /* Battery */
#ifdef _BATTERY
    json_t *battery_json, *j;

    battery_json = json_obj_lookup_key(request_json, "battery");
    uint16 div_factor = 0;
    uint16 voltages[6];
    memset(voltages, 0, sizeof(uint16) * 6);
    if (battery_json) {

        if (json_get_type(battery_json) != JSON_TYPE_OBJ) {
            return INVALID_FIELD(response_json, "battery");
        }

        json = json_obj_lookup_key(battery_json, "div");
        if (!json || json_get_type(json) != JSON_TYPE_INT) {
            return INVALID_FIELD(response_json, "battery");
        }
        div_factor = json_int_get(json);

        json = json_obj_lookup_key(battery_json, "voltages");
        if (!json || json_get_type(json) != JSON_TYPE_LIST || json_list_get_len(json) != BATTERY_LUT_LEN) {
            return INVALID_FIELD(response_json, "battery");
        }

        for (int i = 0; i < BATTERY_LUT_LEN; i++) {
            j = json_list_value_at(json, i);
            if (json_get_type(j) != JSON_TYPE_INT) {
                return INVALID_FIELD(response_json, "battery");
            }

            voltages[i] = json_int_get(j);
        }
    }

    battery_configure(div_factor, voltages);
#endif

    /* Clear existing configuration name & version, but only when called directly, i.e. not during provisioning */
    if (!config_is_provisioning()) {
        DEBUG_DEVICE("clearing config name");
        device_config_name[0] = '\0';
        device_provisioning_version = 0;
    }

    system_config_save();

    *code = 204;

    return response_json;
}

json_t *api_get_provisioning(json_t *query_json, int *code) {
    json_t *response_json = json_obj_new();

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_ADMIN);
    }

    int dummy_code;

    json_obj_append(response_json, "peripherals", api_get_peripherals(NULL, &dummy_code));
    json_obj_append(response_json, "system", api_get_system(NULL, &dummy_code));
    json_obj_append(response_json, "device", api_get_device(NULL, &dummy_code));
    json_obj_append(response_json, "ports", api_get_ports(NULL, &dummy_code));

    *code = 200;

    return response_json;
}

json_t *api_put_provisioning(json_t *query_json, json_t *request_json, int *code) {
    json_t *response_json = json_obj_new();

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(response_json, API_ACCESS_LEVEL_ADMIN);
    }

    if (json_get_type(request_json) != JSON_TYPE_OBJ) {
        return API_ERROR(response_json, 400, "invalid-request");
    }

    if (!config_apply_json_provisioning(request_json, /* force = */ TRUE)) {
        return API_ERROR(response_json, 400, "invalid-request");
    }

    /* Clear existing configuration name & version */
    DEBUG_DEVICE("clearing config name");
    device_config_name[0] = '\0';
    device_provisioning_version = 0;

    event_push_full_update();

    *code = 204;

    return response_json;
}

json_t *_api_error(json_t *response_json, char *error, char *field_name, char *field_value) {
    json_free(response_json);
    response_json = json_obj_new();
    json_obj_append(response_json, "error", json_str_new(error));
    if (field_name) {
        json_obj_append(response_json, field_name, json_str_new(field_value));
    }
    return response_json;
}

json_t *_forbidden_error(json_t *response_json, uint8 level) {
    char *field_value;

    switch (level) {
        case API_ACCESS_LEVEL_ADMIN:
            field_value = "admin";
            break;

        case API_ACCESS_LEVEL_NORMAL:
            field_value = "normal";
            break;

        case API_ACCESS_LEVEL_VIEWONLY:
            field_value = "viewonly";
            break;

        default:
            field_value = "none";
            break;
    }

    return _api_error(response_json, "forbidden", "required_level", field_value);
}

json_t *_invalid_expression_error(json_t *response_json, char *field, char *reason, char *token, int32 pos) {
    json_free(response_json);
    response_json = json_obj_new();
    json_obj_append(response_json, "error", json_str_new("invalid-field"));
    json_obj_append(response_json, "field", json_str_new(field));

    json_t *details_json = json_obj_new();
    json_obj_append(details_json, "reason", json_str_new(reason));
    if (token) {
        json_obj_append(details_json, "token", json_str_new(token));
    }
    if (pos >= 1) {
        json_obj_append(details_json, "pos", json_int_new(pos));
    }
    json_obj_append(response_json, "details", details_json);

    return response_json;
}


json_t *device_from_json(
    json_t *json,
    int *code,
    bool *needs_reset,
    bool *needs_sleep_reset,
    bool *config_name_changed,
    bool ignore_unknown
) {
    *needs_reset = FALSE;
    *config_name_changed = FALSE;
    *needs_sleep_reset = FALSE;

    json_t *response_json = NULL;

    int i;
    char *key;
    json_t *child;
    for (i = 0; i < json_obj_get_len(json); i++) {
        key = json_obj_key_at(json, i);
        child = json_obj_value_at(json, i);

        if (!strcmp(key, "name")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(response_json, key);
            }

            char *value = json_str_get(child);
            if (!validate_id(value) || strlen(json_str_get(child)) > API_MAX_DEVICE_NAME_LEN) {
                return INVALID_FIELD(response_json, key);
            }

            free(device_name);
            device_name = strdup(value);

            DEBUG_DEVICE("name set to \"%s\"", device_name);

            httpserver_set_name(device_name);
        }
        else if (!strcmp(key, "display_name")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(response_json, key);
            }

            char *value = json_str_get(child);
            if (strlen(json_str_get(child)) > API_MAX_DEVICE_DISP_NAME_LEN) {
                return INVALID_FIELD(response_json, key);
            }

            free(device_display_name);
            device_display_name = strdup(value);

            DEBUG_DEVICE("display name set to \"%s\"", device_display_name);
        }
        else if (!strcmp(key, "admin_password")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(response_json, key);
            }

            char *password = json_str_get(child);
            char *password_hash = sha256_hex(password);
            strcpy(device_admin_password_hash, password_hash);
            free(password_hash);
            DEBUG_DEVICE("admin password set");
        }
        else if (!strcmp(key, "normal_password")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(response_json, key);
            }

            char *password = json_str_get(child);
            char *password_hash = sha256_hex(password);
            strcpy(device_normal_password_hash, password_hash);
            free(password_hash);
            DEBUG_DEVICE("normal password set");
        }
        else if (!strcmp(key, "viewonly_password")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(response_json, key);
            }

            char *password = json_str_get(child);
            char *password_hash = sha256_hex(password);
            strcpy(device_viewonly_password_hash, password_hash);
            free(password_hash);
            DEBUG_DEVICE("view-only password set");
        }
        else if (!strcmp(key, "admin_password_hash")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(response_json, key);
            }

            char *password_hash = json_str_get(child);
            strcpy(device_admin_password_hash, password_hash);
            DEBUG_DEVICE("admin password set");
        }
        else if (!strcmp(key, "normal_password_hash")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(response_json, key);
            }

            char *password_hash = json_str_get(child);
            strcpy(device_normal_password_hash, password_hash);
            DEBUG_DEVICE("normal password set");
        }
        else if (!strcmp(key, "viewonly_password_hash")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(response_json, key);
            }

            char *password_hash = json_str_get(child);
            strcpy(device_viewonly_password_hash, password_hash);
            DEBUG_DEVICE("view-only password set");
        }
        else if (!strcmp(key, "ip_address")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(response_json, key);
            }

            char *ip_address_str = json_str_get(child);
            ip_addr_t ip_address = {0};
            if (ip_address_str[0]) { /* Manual */
                uint8 bytes[4];
                if (!validate_ip_address(ip_address_str, bytes)) {
                    return INVALID_FIELD(response_json, key);
                }

                IP4_ADDR(&ip_address, bytes[0], bytes[1], bytes[2], bytes[3]);
            }

            wifi_set_ip_address(ip_address);
            *needs_reset = TRUE;
        }
        else if (!strcmp(key, "ip_netmask")) {
            if (json_get_type(child) != JSON_TYPE_INT) {
                return INVALID_FIELD(response_json, key);
            }

            int netmask = json_int_get(child);
            if (!validate_num(netmask, 0, 31, /* integer = */ TRUE, /* step = */ 0, /* choices = */ NULL)) {
                return INVALID_FIELD(response_json, key);
            }

            wifi_set_netmask(netmask);
            *needs_reset = TRUE;
        }
        else if (!strcmp(key, "ip_gateway")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(response_json, key);
            }

            char *gateway_str = json_str_get(child);
            ip_addr_t gateway = {0};
            if (gateway_str[0]) { /* Manual */
                uint8 bytes[4];
                if (!validate_ip_address(gateway_str, bytes)) {
                    return INVALID_FIELD(response_json, key);
                }

                IP4_ADDR(&gateway, bytes[0], bytes[1], bytes[2], bytes[3]);
            }

            wifi_set_gateway(gateway);
            *needs_reset = TRUE;
        }
        else if (!strcmp(key, "ip_dns")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(response_json, key);
            }

            char *dns_str = json_str_get(child);
            ip_addr_t dns = {0};
            if (dns_str[0]) { /* Manual */
                uint8 bytes[4];
                if (!validate_ip_address(dns_str, bytes)) {
                    return INVALID_FIELD(response_json, key);
                }

                IP4_ADDR(&dns, bytes[0], bytes[1], bytes[2], bytes[3]);
            }

            wifi_set_dns(dns);
            *needs_reset = TRUE;
        }
        else if (!strcmp(key, "wifi_ssid")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(response_json, key);
            }

            char *ssid = json_str_get(child);
            if (!validate_wifi_ssid(ssid)) {
                return INVALID_FIELD(response_json, key);
            }

            wifi_set_ssid(ssid);
            *needs_reset = TRUE;
        }
        else if (!strcmp(key, "wifi_key")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(response_json, key);
            }

            char *psk = json_str_get(child);
            if (!validate_wifi_key(psk)) {
                return INVALID_FIELD(response_json, key);
            }

            wifi_set_psk(psk);
            *needs_reset = TRUE;
        }
        else if (!strcmp(key, "wifi_bssid")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(response_json, key);
            }

            char *bssid_str = json_str_get(child);
            if (bssid_str[0]) {
                uint8 bssid[WIFI_BSSID_LEN];
                if (!validate_wifi_bssid(bssid_str, bssid)) {
                    return INVALID_FIELD(response_json, key);
                }

                wifi_set_bssid(bssid);
            }
            else {
                wifi_set_bssid(NULL);
            }
            *needs_reset = TRUE;
        }
#ifdef _SLEEP
        else if (!strcmp(key, "sleep_wake_interval")) {
            if (json_get_type(child) != JSON_TYPE_INT) {
                return INVALID_FIELD(response_json, key);
            }

            uint32 interval = json_int_get(child);
            bool valid = validate_num(
                interval,
                SLEEP_WAKE_INTERVAL_MIN,
                SLEEP_WAKE_INTERVAL_MAX,
                /* integer = */ TRUE,
                /* step = */ 0,
                /* choices = */ NULL
            );
            if (!valid) {
                return INVALID_FIELD(response_json, key);
            }

            sleep_set_wake_interval(interval);

            *needs_sleep_reset = TRUE;
        }
        else if (!strcmp(key, "sleep_wake_duration")) {
            if (json_get_type(child) != JSON_TYPE_INT) {
                return INVALID_FIELD(response_json, key);
            }

            uint32 duration = json_int_get(child);
            bool valid = validate_num(
                duration,
                SLEEP_WAKE_DURATION_MIN,
                SLEEP_WAKE_DURATION_MAX,
                /* integer = */ TRUE,
                /* step = */ 0,
                /* choices = */ NULL
            );
            if (!valid) {
                return INVALID_FIELD(response_json, key);
            }

            sleep_set_wake_duration(duration);

            *needs_sleep_reset = TRUE;
        }
#endif
#ifdef _OTA
        else if (!strcmp(key, "firmware_auto_update")) {
            if (json_get_type(child) != JSON_TYPE_BOOL) {
                return INVALID_FIELD(response_json, key);
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
                return INVALID_FIELD(response_json, key);
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
        else if (!strcmp(key, "config_name")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD(response_json, key);
            }

            char *config_name = json_str_get(child);
            strncpy(device_config_name, config_name, API_MAX_DEVICE_CONFIG_NAME_LEN);
            device_config_name[API_MAX_DEVICE_CONFIG_NAME_LEN - 1] = 0;

            *config_name_changed = TRUE;
            DEBUG_DEVICE("config name set to \"%s\"", device_config_name);

            device_provisioning_version = 0; /* Also reset the provisioning version */
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
                 !strcmp(key, "flash_id")) {

            if (!ignore_unknown) {
                return ATTR_NOT_MODIFIABLE(response_json, key);
            }
        }
        else {
            if (!ignore_unknown) {
                return NO_SUCH_ATTR(response_json, key);
            }
        }
    }

    *code = 204;

    return json_obj_new();
}

json_t *port_attrdefs_to_json(port_t *port, json_refs_ctx_t *json_refs_ctx) {
    json_t *json = json_obj_new();
    json_t *attrdef_json;

    if (port->attrdefs) {
        int8 found_port_index = -1;
        if (json_refs_ctx->type == JSON_REFS_TYPE_PORTS_LIST) {
            /* Look through previous ports for ports having exact same attrdefs */
            port_t *p = NULL;
            uint8 i = 0;
            for (i = 0; i < all_ports_count && p != port; i++) {
                p = all_ports[i];
                if ((p->attrdefs == port->attrdefs) &&
                    (IS_PORT_WRITABLE(p) == IS_PORT_WRITABLE(port)) &&
                    (IS_PORT_VIRTUAL(p) == IS_PORT_VIRTUAL(port)) &&
                    (p->type == port->type)) {

                    found_port_index = i;
                    break;
                }
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

            attrdef_json = attrdef_to_json(
                a->display_name ? a->display_name : "",
                a->description ? a->description : "",
                a->unit,
                a->type,
                IS_ATTRDEF_MODIFIABLE(a),
                a->min,
                a->max,
                IS_ATTRDEF_INTEGER(a),
                a->step,
                choices,
                IS_ATTRDEF_RECONNECT(a)
            );

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
                        ref = make_json_ref(
                            "#/%d/params/definitions/%s/choices",
                            json_refs_ctx->index,
                            found_attrdef_name
                        );
                        break;

                    case JSON_REFS_TYPE_WEBHOOKS_EVENT:
                        ref = make_json_ref("#/params/definitions/%s/choices", found_attrdef_name);
                        break;
                }
#if defined(_DEBUG) && defined(_DEBUG_API)
                char *ref_str = json_str_get(json_obj_value_at(ref, 0));
                DEBUG_API(
                    "replacing \"%s.definitions.%s.choices\" with $ref \"%s\"",
                    port->id,
                    found_attrdef_name,
                    ref_str
                );
#endif
                json_obj_append(attrdef_json, "choices", ref);
            }

            json_stringify(attrdef_json);
            json_obj_append(json, a->name, attrdef_json);
        }
    }

    if (!IS_PORT_VIRTUAL(port) && port->min_sampling_interval < port->max_sampling_interval) {
        // FIXME: JSON refs are wrong when sampling_interval fields (particularly min/max) differ

        /* sampling_interval attrdef */
//        if (json_refs_ctx->type == JSON_REFS_TYPE_PORTS_LIST && json_refs_ctx->sampling_interval_port_index >= 0) {
//            attrdef_json = make_json_ref(
//                "#/%d/definitions/sampling_interval",
//                json_refs_ctx->sampling_interval_port_index
//            );
//#if defined(_DEBUG) && defined(_DEBUG_API)
//            char *ref_str = json_str_get(json_obj_value_at(attrdef_json, 0));
//            DEBUG_API("replacing \"%s.definitions.sampling_interval\" with $ref \"%s\"", port->id, ref_str);
//#endif
//        }
//        else {
            attrdef_json = attrdef_to_json(
                "Sampling Interval",
                "Indicates how often the port value will be read.",
                "ms",
                ATTR_TYPE_NUMBER,
                /* modifiable = */ TRUE,
                port->min_sampling_interval,
                port->max_sampling_interval,
                /* integer = */ TRUE ,
                /* step = */ 0,
                /* choices = */ NULL,
                /* reconnect = */ FALSE
            );
//            if (json_refs_ctx->type == JSON_REFS_TYPE_PORTS_LIST) {
//                json_refs_ctx->sampling_interval_port_index = json_refs_ctx->index;
//            }
//        }
        json_obj_append(json, "sampling_interval", attrdef_json);
    }

    json_stringify(json);

    return json;
}

json_t *device_attrdefs_to_json(void) {
    json_t *json = json_obj_new();
    json_t *attrdef_json;

#ifdef _SLEEP
    attrdef_json = attrdef_to_json(
        "Sleep Wake Interval",
        "Controls how often the device will wake from sleep (0 disables sleep mode).",
        /* unit = */ "min",
        ATTR_TYPE_NUMBER,
        /* modifiable = */ TRUE,
        /* min = */ SLEEP_WAKE_INTERVAL_MIN,
        /* max = */ SLEEP_WAKE_INTERVAL_MAX,
        /* integer = */ TRUE,
        /* step = */ 0,
        /* choices = */ NULL,
        /* reconnect = */ FALSE
    );
    json_obj_append(json, "sleep_wake_interval", attrdef_json);

    attrdef_json = attrdef_to_json(
        "Sleep Wake Duration",
        "Controls for how long the device stays awake (0 disables sleep mode).",
        /* unit = */ "s",
        ATTR_TYPE_NUMBER,
        /* modifiable = */ TRUE,
        /* min = */ SLEEP_WAKE_DURATION_MIN,
        /* max = */ SLEEP_WAKE_DURATION_MAX,
        /* integer = */ TRUE,
        /* step = */ 0,
        /* choices = */ NULL,
        /* reconnect = */ FALSE
    );
    json_obj_append(json, "sleep_wake_duration", attrdef_json);
#endif

#ifdef _OTA
    attrdef_json = attrdef_to_json(
        "Firmware Auto-update",
        "Enables automatic firmware update when detecting a newer version.",
        /* unit = */ NULL,
        ATTR_TYPE_BOOLEAN,
        /* modifiable = */ TRUE,
        /* min = */ UNDEFINED,
        /* max = */ UNDEFINED,
        /* integer = */ FALSE,
        /* step = */ 0,
        /* choices = */ NULL,
        /* reconnect = */ FALSE
    );
    json_obj_append(json, "firmware_auto_update", attrdef_json);

    attrdef_json = attrdef_to_json(
        "Firmware Beta Versions",
        "Enables access to beta firmware releases. Leave this disabled unless you know what you're doing.",
        /* unit = */ NULL, ATTR_TYPE_BOOLEAN,
        /* modifiable = */ TRUE,
        /* min = */ UNDEFINED,
        /* max = */ UNDEFINED,
        /* integer = */ FALSE,
        /* step = */ 0,
        /* choices = */ NULL,
        /* reconnect = */ FALSE
    );
    json_obj_append(json, "firmware_beta_enabled", attrdef_json);
#endif

#ifdef _BATTERY
    if (battery_enabled()) {
        attrdef_json = attrdef_to_json(
            "Battery Voltage",
            "The battery voltage.",
            "mV",
            ATTR_TYPE_NUMBER,
            /* modifiable = */ FALSE,
            /* min = */ UNDEFINED,
            /* max = */ UNDEFINED,
            /* integer = */ FALSE,
            /* step = */ 0,
            /* choices = */ NULL,
            /* reconnect = */ FALSE
        );
        json_obj_append(json, "battery_voltage", attrdef_json);
    }
#endif

    attrdef_json = attrdef_to_json(
        "Flash Size",
        "Total flash memory size.",
        "kB",
        ATTR_TYPE_NUMBER,
        /* modifiable = */ FALSE,
        /* min = */ UNDEFINED,
        /* max = */ UNDEFINED,
        /* integer = */ TRUE,
        /* step = */ 0,
        /* choices = */ NULL,
        /* reconnect = */ FALSE
    );
    json_obj_append(json, "flash_size", attrdef_json);

    attrdef_json = attrdef_to_json(
        "Flash ID",
        "Device flash model identifier.",
        /* unit = */ NULL,
        ATTR_TYPE_STRING,
        /* modifiable = */ FALSE,
        /* min = */ UNDEFINED,
        /* max = */ UNDEFINED,
        /* integer = */ FALSE,
        /* step = */ 0,
        /* choices = */ NULL,
        /* reconnect = */ FALSE
    );
    json_obj_append(json, "flash_id", attrdef_json);

    attrdef_json = attrdef_to_json(
        "Chip ID",
        "Device chip identifier.",
        /* unit = */ NULL,
        ATTR_TYPE_STRING,
        /* modifiable = */ FALSE,
        /* min = */ UNDEFINED,
        /* max = */ UNDEFINED,
        /* integer = */ FALSE,
        /* step = */ 0,
        /* choices = */ NULL,
        /* reconnect = */ FALSE
    );
    json_obj_append(json, "chip_id", attrdef_json);

#ifdef _DEBUG
    attrdef_json = attrdef_to_json(
        "Debug",
        "Indicates that debugging was enabled when the firmware was built.",
        /* unit = */ NULL,
        ATTR_TYPE_BOOLEAN,
        /* modifiable = */ FALSE,
        /* min = */ UNDEFINED,
        /* max = */ UNDEFINED,
        /* integer = */ FALSE,
        /* step = */ 0,
        /* choices = */ NULL,
        /* reconnect = */ FALSE
    );
    json_obj_append(json, "debug", attrdef_json);
#endif

    return json;
}

void on_sequence_timer(void *arg) {
    port_t *port = arg;

    if (port->sequence_pos < port->sequence_len) {
        port_write_value(port, port->sequence_values[port->sequence_pos], CHANGE_REASON_SEQUENCE);

        DEBUG_PORT(port, "sequence delay of %d ms", port->sequence_delays[port->sequence_pos]);

        os_timer_arm(port->sequence_timer, port->sequence_delays[port->sequence_pos], /* repeat = */ FALSE);
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
            port_sequence_cancel(port);
            DEBUG_PORT(port, "sequence done");

            if (IS_PORT_PERSISTED(port)) {
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

    free(version);
    free(date);
    free(url);
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
        snprintf(
            bssid,
            sizeof(bssid),
            "%02X:%02X:%02X:%02X:%02X:%02X",
            results[i].bssid[0],
            results[i].bssid[1],
            results[i].bssid[2],
            results[i].bssid[3],
            results[i].bssid[4],
            results[i].bssid[5]
        );
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
