
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
#include "espgoodies/httpserver.h"
#include "espgoodies/wifi.h"
#include "espgoodies/crypto.h"
#include "espgoodies/pingwdt.h"
#include "espgoodies/system.h"
#include "espgoodies/flashcfg.h"
#include "espgoodies/utils.h"

#ifdef _BATTERY
#include "espgoodies/battery.h"
#endif

#ifdef _SLEEP
#include "espgoodies/sleep.h"
#endif

#ifdef _OTA
#include "espgoodies/ota.h"
#endif

#include "ports.h"
#include "config.h"
#include "sessions.h"
#include "client.h"
#include "webhooks.h"
#include "device.h"
#include "core.h"
#include "ver.h"
#include "api.h"


#define API_ERROR(c, error) ({                                                          \
    if (response_json) json_free(response_json);                                        \
    json_obj_append(response_json = json_obj_new(), "error", json_str_new(error));      \
    *code = c;                                                                          \
    response_json;                                                                      \
})

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

#define INVALID_FIELD_VALUE(field) ({                                                   \
    char error[API_MAX_FIELD_VALUE_LEN];                                                \
    snprintf(error, API_MAX_FIELD_VALUE_LEN, "invalid field: %s", field);               \
    API_ERROR(400, error);                                                              \
})

#define MISSING_FIELD(field) ({                                                         \
    char error[API_MAX_FIELD_VALUE_LEN];                                                \
    snprintf(error, API_MAX_FIELD_VALUE_LEN, "missing field: %s", field);               \
    API_ERROR(400, error);                                                              \
})

#define SEQ_INVALID_FIELD_VALUE(field) ({                                               \
    free(port->sequence_values);                                                        \
    free(port->sequence_delays);                                                        \
    port->sequence_pos = -1;                                                            \
    port->sequence_repeat = -1;                                                         \
    INVALID_FIELD_VALUE(field);                                                         \
})

#define ATTR_NOT_MODIFIABLE(attr) ({                                                    \
    char error[API_MAX_FIELD_VALUE_LEN];                                                \
    snprintf(error, API_MAX_FIELD_VALUE_LEN, "attribute not modifiable: %s", attr);     \
    API_ERROR(400, error);                                                              \
})

#define NO_SUCH_ATTRIBUTE(attr) ({                                                      \
    char error[API_MAX_FIELD_VALUE_LEN];                                                \
    snprintf(error, API_MAX_FIELD_VALUE_LEN, "no such attribute: %s", attr);            \
    API_ERROR(400, error);                                                              \
})

#define RESPOND_NO_SUCH_FUNCTION() {                                                    \
    API_ERROR(404, "no such function");                                                 \
    goto response;                                                                      \
}


static uint8            api_access_level = 0;
static struct espconn * api_conn;

static char           * frequency_choices[] = {"80", "160", NULL};
static char           * ping_watchdog_interval_choices[] = {"0", "1", "5", "10", "30", "60", "120", NULL};

#ifdef _OTA
static char           * ota_states_str[] = {"idle", "checking", "downloading", "restarting"};
#endif


ICACHE_FLASH_ATTR static json_t       * get_device(json_t *query_json, int *code);
ICACHE_FLASH_ATTR static json_t       * patch_device(json_t *query_json, json_t *request_json, int *code);

ICACHE_FLASH_ATTR static json_t       * post_reset(json_t *query_json, json_t *request_json, int *code);
#ifdef _OTA
ICACHE_FLASH_ATTR static json_t       * get_firmware(json_t *query_json, int *code);
ICACHE_FLASH_ATTR static json_t       * patch_firmware(json_t *query_json, json_t *request_json, int *code);
#endif
ICACHE_FLASH_ATTR static json_t       * get_access(json_t *query_json, int *code);
ICACHE_FLASH_ATTR static json_t       * get_ports(json_t *query_json, int *code);
ICACHE_FLASH_ATTR static json_t       * post_ports(json_t *query_json, json_t *request_json, int *code);
ICACHE_FLASH_ATTR static json_t       * patch_port(port_t *port, json_t *query_json, json_t *request_json, int *code);
ICACHE_FLASH_ATTR static json_t       * delete_port(port_t *port, json_t *query_json, int *code);

ICACHE_FLASH_ATTR static json_t       * get_port_value(port_t *port, json_t *query_json, int *code);
ICACHE_FLASH_ATTR static json_t       * patch_port_value(port_t *port, json_t *query_json, json_t *request_json,
                                                         int *code);
ICACHE_FLASH_ATTR static json_t       * post_port_sequence(port_t *port, json_t *query_json, json_t *request_json,
                                                           int *code);

ICACHE_FLASH_ATTR static json_t       * get_webhooks(json_t *query_json, int *code);
ICACHE_FLASH_ATTR static json_t       * patch_webhooks(json_t *query_json, json_t *request_json, int *code);

ICACHE_FLASH_ATTR static json_t       * get_wifi(json_t *query_json, int *code);

ICACHE_FLASH_ATTR static json_t       * port_attrdefs_to_json(port_t *port);
ICACHE_FLASH_ATTR static json_t       * device_attrdefs_to_json(void);

ICACHE_FLASH_ATTR static json_t       * attrdef_to_json(char *display_name, char *description, char *unit, char type,
                                                        bool modifiable, double min, double max, bool integer,
                                                        double step, char **choices, bool reconnect);

ICACHE_FLASH_ATTR static json_t       * choice_to_json(char *choice, char type);
ICACHE_FLASH_ATTR static void           free_choices(char **choices);

ICACHE_FLASH_ATTR static bool           validate_num(double value, double min, double max, bool integer,
                                                     double step, char **choices);
ICACHE_FLASH_ATTR static bool           validate_str(char *value, char **choices);
ICACHE_FLASH_ATTR static bool           validate_id(char *id);
ICACHE_FLASH_ATTR static bool           validate_str_ip(char *ip, uint8 *a, int len);
ICACHE_FLASH_ATTR static bool           validate_str_wifi(char *wifi, char *ssid, char *psk, uint8 *bssid);
ICACHE_FLASH_ATTR static bool           validate_str_network_scan(char *scan, int *scan_interval, int *scan_threshold);
#ifdef _SLEEP
ICACHE_FLASH_ATTR static bool           validate_str_sleep_mode(char *sleep_mode, int *wake_interval,
                                                                int *wake_duration);
#endif

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

    /* remove the leading & trailing slashes */

    while (path[0] == '/') {
        path++;
    }

    int path_len = strlen(path);
    while (path[path_len - 1] == '/') {
        path[path_len - 1] = 0;
        path_len--;
    }

    /* split path */

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


    /* determine the api call */

    if (!strcmp(part1, "device")) {
        if (part2) {
            RESPOND_NO_SUCH_FUNCTION();
        }
        else {
            if (method == HTTP_METHOD_GET) {
                response_json = get_device(query_json, code);
            }
            else if (method == HTTP_METHOD_PATCH) {
                response_json = patch_device(query_json, request_json, code);
            }
            else {
                RESPOND_NO_SUCH_FUNCTION();
            }
        }
    }
    else if (!strcmp(part1, "reset") && method == HTTP_METHOD_POST && !part2) {
        response_json = post_reset(query_json, request_json, code);
    }
#ifdef _OTA
    else if (!strcmp(part1, "firmware") && !part2) {
        if (method == HTTP_METHOD_GET) {
            response_json = get_firmware(query_json, code);
        }
        else if (method == HTTP_METHOD_PATCH) {
            response_json = patch_firmware(query_json, request_json, code);
        }
        else {
            RESPOND_NO_SUCH_FUNCTION();
        }
    }
#endif
    else if (!strcmp(part1, "access")) {
        if (!part2) {
            response_json = get_access(query_json, code);
        }
        else {
            RESPOND_NO_SUCH_FUNCTION();
        }
    }
    else if (!strcmp(part1, "ports")) {
        if (part2) {
            port = port_find_by_id(part2);
            if (!port) {
                API_ERROR(404, "no such port");
                goto response;
            }

            if (part3) {
                if (!strcmp(part3, "value")) { /* /ports/{id}/value */
                    if (method == HTTP_METHOD_GET) {
                        response_json = get_port_value(port, query_json, code);
                    }
                    else if (method == HTTP_METHOD_PATCH) {
                        response_json = patch_port_value(port, query_json, request_json, code);
                    }
                    else {
                        RESPOND_NO_SUCH_FUNCTION();
                    }
                }
                else if (!strcmp(part3, "sequence")) { /* /ports/{id}/sequence */
                    if (method == HTTP_METHOD_POST) {
                        response_json = post_port_sequence(port, query_json, request_json, code);
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
                    response_json = patch_port(port, query_json, request_json, code);
                }
#ifdef HAS_VIRTUAL
                else if (method == HTTP_METHOD_DELETE) {
                    response_json = delete_port(port, query_json, code);
                }
#endif
                else {
                    RESPOND_NO_SUCH_FUNCTION();
                }
            }
        }
        else { /* /ports */
            if (method == HTTP_METHOD_GET) {
                response_json = get_ports(query_json, code);
            }
#ifdef HAS_VIRTUAL
            else if (method == HTTP_METHOD_POST) {
                response_json = post_ports( query_json, request_json, code);
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
            response_json = get_webhooks(query_json, code);
        }
        else if (method == HTTP_METHOD_PATCH) {
            response_json = patch_webhooks(query_json, request_json, code);
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
            response_json = get_wifi(query_json, code);
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


json_t *port_to_json(port_t *port) {
    json_t *json = json_obj_new();

    /* common to all ports */

    json_obj_append(json, "id", json_str_new(port->id));
    json_obj_append(json, "display_name", json_str_new(port->display_name ? port->display_name : ""));
    json_obj_append(json, "unit", json_str_new(port->unit ? port->unit : ""));
    json_obj_append(json, "writable", json_bool_new(IS_OUTPUT(port)));
    json_obj_append(json, "enabled", json_bool_new(IS_ENABLED(port)));
    json_obj_append(json, "persisted", json_bool_new(IS_PERSISTED(port)));

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

    /* specific to numeric ports */
    if (port->type == PORT_TYPE_NUMBER) {
        json_obj_append(json, "type", json_str_new(API_PORT_TYPE_NUMBER));

        if (!IS_OUTPUT(port)) {
            json_obj_append(json, "filter", json_str_new(filter_choices[FILTER_TYPE(port) >> 4]));
            json_obj_append(json, "filter_width", json_int_new(port->filter_width));
        }

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
            json_t *list = json_list_new();
            char *c, **choices = port->choices;
            while ((c = *choices++)) {
                json_list_append(list, choice_to_json(c, PORT_TYPE_NUMBER));
            }

            json_obj_append(json, "choices", list);
        }
    }
    /* specific to boolean ports */
    else { /* assuming PORT_TYPE_BOOLEAN */
        json_obj_append(json, "type", json_str_new(API_PORT_TYPE_BOOLEAN));

        if (!IS_OUTPUT(port)) {
            if (FILTER_TYPE(port)) {
                uint32 sampling_interval = MAX(1, port->sampling_interval);
                uint32 debounce = port->filter_width * sampling_interval;
                debounce = MAX(1, MIN(debounce, PORT_MAX_DEBOUNCE_TIME));
                json_obj_append(json, "debounce", json_int_new(debounce));
            }
            else {
                json_obj_append(json, "debounce", json_int_new(0));
            }
        }
    }

    /* specific to output ports */
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

    /* extra attributes */
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
                        /* when dealing with choices, the getter returns the index inside the choices array */
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
                        /* when dealing with choices, the getter returns the index inside the choices array */
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

    /* port value */
    json_obj_append(json, "value", port_get_json_value(port));

    /* attribute definitions */
    json_obj_append(json, "definitions", port_attrdefs_to_json(port));

    json_stringify(json);

    return json;
}

json_t *device_to_json(void) {
    char value[256];
    json_t *json = json_obj_new();

    /* common attributes */
    json_obj_append(json, "name", json_str_new(device_hostname));
    json_obj_append(json, "display_name", json_str_new(device_display_name));
    json_obj_append(json, "version", json_str_new(FW_VERSION));
    json_obj_append(json, "api_version", json_str_new(API_VERSION));
    json_obj_append(json, "vendor", json_str_new(VENDOR));

    /* passwords - never reveal them */
    json_obj_append(json, "admin_password", json_str_new(""));
    json_obj_append(json, "normal_password", json_str_new(""));
    json_obj_append(json, "viewonly_password", json_str_new(""));

    /* flags */
    json_t *flags_json = json_list_new();

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

    /* various optional attributes */
    json_obj_append(json, "uptime", json_int_new(system_uptime()));

#ifdef HAS_VIRTUAL
    json_obj_append(json, "virtual_ports", json_int_new(VIRTUAL_MAX_PORTS));
#endif

    /* network_ip */
    if (wifi_get_static_ip()) {
        snprintf(value, 256, "%d.%d.%d.%d/%d:%d.%d.%d.%d:%d.%d.%d.%d",
                 IP2STR(wifi_get_static_ip()),
                 wifi_get_static_netmask(),
                 IP2STR(wifi_get_static_gw()),
                 IP2STR(wifi_get_static_dns()));
        json_obj_append(json, "network_ip", json_str_new(value));
    }
    else {
        json_obj_append(json, "network_ip", json_str_new(""));
    }

    /* network_wifi */
    char *ssid = wifi_get_ssid();
    char *psk = wifi_get_psk();
    uint8 *bssid = wifi_get_bssid();
    if (bssid[0]) {
        snprintf(value, 256, "%s:%s:%02X%02X%02X%02X%02X%02X", ssid, psk, BSSID2STR(bssid));
    }
    else if (psk[0]) {  /* no bssid */
        snprintf(value, 256, "%s:%s", ssid, psk);
    }
    else {  /* no bssid, no psk */
        snprintf(value, 256, "%s", ssid);
    }
    json_obj_append(json, "network_wifi", json_str_new(value));

    /* rssi */
    int rssi = wifi_station_get_rssi();
    if (rssi < -100) {
        rssi = -100;
    }
    if (rssi > -30) {
        rssi = -30;
    }

    /* bssid */
    struct station_config conf;
    wifi_station_get_config(&conf);
    char bssid_str[18] = {0};

    if (wifi_station_get_connect_status() == STATION_GOT_IP) {
        snprintf(bssid_str, sizeof(bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 conf.bssid[0], conf.bssid[1], conf.bssid[2],
                 conf.bssid[3], conf.bssid[4], conf.bssid[5]);
    }

    json_obj_append(json, "frequency", json_int_new(system_get_cpu_freq()));
    json_obj_append(json, "network_rssi", json_int_new(rssi));
    json_obj_append(json, "network_bssid", json_str_new(bssid_str));
    json_obj_append(json, "free_mem", json_int_new(system_get_free_heap_size() / 1024));
    json_obj_append(json, "flash_size", json_int_new(system_get_flash_size() / 1024));

    /* network scan */
    int wifi_scan_interval = wifi_get_scan_interval();
    int wifi_scan_threshold = wifi_get_scan_threshold();

    if (!wifi_scan_interval) {
        json_obj_append(json, "network_scan", json_str_new(""));
    }
    else {
        snprintf(value, 256, "%d:%d", wifi_scan_interval, wifi_scan_threshold);
    }

#ifdef _OTA
    json_obj_append(json, "firmware_auto_update", json_bool_new(device_flags & DEVICE_FLAG_OTA_AUTO_UPDATE));
#endif

    /* ping watchdog */
    json_obj_append(json, "ping_watchdog_interval", json_int_new(ping_wdt_get_interval()));

    /* flash id */
    char id[10];
    snprintf(id, 10, "%08x", spi_flash_get_id());
    json_obj_append(json, "flash_id", json_str_new(id));

    /* chip id */
    snprintf(id, 10, "%08x", system_get_chip_id());
    json_obj_append(json, "chip_id", json_str_new(id));

    /* config name & model */
    if (device_config_model_choices[0]) {
        char config_name[64];
        snprintf(config_name, 64, "%s/%s", FW_CONFIG_NAME, device_config_model);
        json_obj_append(json, "config_name", json_str_new(config_name));
        json_obj_append(json, "config_model", json_str_new(device_config_model));
    }
    else {
        json_obj_append(json, "config_name", json_str_new(FW_CONFIG_NAME));
    }

#ifdef _DEBUG
    json_obj_append(json, "debug", json_bool_new(TRUE));
#endif

    /* sleep mode */
#ifdef _SLEEP
    if (sleep_get_wake_interval()) {
        snprintf(value, 256, "%d:%d", sleep_get_wake_interval(), sleep_get_wake_duration());
    }
    else {
        value[0] = 0;
    }
    json_obj_append(json, "sleep_mode", json_str_new(value));
#endif

    /* battery */
#ifdef _BATTERY
    json_obj_append(json, "battery_level", json_int_new(battery_get_level()));
    json_obj_append(json, "battery_voltage", json_int_new(battery_get_voltage()));
#endif

    /* attribute definitions */
    json_obj_append(json, "definitions", device_attrdefs_to_json());

    json_stringify(json);

    return json;
}

double get_choice_value_num(char *choice) {
    return strtod(get_choice_value_str(choice), NULL);
}

char *get_choice_value_str(char *choice) {
    static char *value = NULL;  /* acts as a reentrant buffer */
    if (value) {
        free(value);
        value = NULL;
    }

    char *p = strchr(choice, ':');
    if (p) {
        value = strndup(choice, p - choice);
    }
    else {
        return choice;
    }

    return value;
}

char *get_choice_display_name(char *choice) {
    static char *display_name = NULL;  /* acts as a reentrant buffer */
    if (display_name) {
        free(display_name);
        display_name = NULL;
    }

    char *p = strchr(choice, ':');
    if (p) {
        display_name = strdup(p + 1);
    }
    else {
        return NULL;
    }

    return display_name;
}


json_t *get_device(json_t *query_json, int *code) {
    json_t *response_json = NULL;

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(API_ACCESS_LEVEL_ADMIN);
    }

    DEBUG_API("returning device attributes");

    *code = 200;
    
    return device_to_json();
}

json_t *patch_device(json_t *query_json, json_t *request_json, int *code) {
    DEBUG_API("updating device attributes");

    json_t *response_json = json_obj_new();
    
    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(API_ACCESS_LEVEL_ADMIN);
    }

    if (json_get_type(request_json) != JSON_TYPE_OBJ) {
        return API_ERROR(400, "invalid request");
    }

    int i;
    bool needs_reset = FALSE;
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
                return INVALID_FIELD_VALUE(key);
            }
            
            char *value = json_str_get(child);
            if (!validate_id(value)) {
                return INVALID_FIELD_VALUE(key);
            }
            
            strncpy(device_hostname, value, API_MAX_DEVICE_NAME_LEN);
            device_hostname[API_MAX_DEVICE_NAME_LEN - 1] = 0;
            
            DEBUG_DEVICE("name set to %s", device_hostname);

            httpserver_set_name(device_hostname);
        }
        else if (!strcmp(key, "display_name")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD_VALUE(key);
            }

            strncpy(device_display_name, json_str_get(child), API_MAX_DEVICE_DISP_NAME_LEN);
            device_display_name[API_MAX_DEVICE_DISP_NAME_LEN - 1] = 0;
            
            DEBUG_DEVICE("display name set to \"%s\"", device_display_name);
        }
        else if (!strcmp(key, "admin_password")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD_VALUE(key);
            }
            
            char *password = json_str_get(child);
            char *password_hash = sha256_hex(password);
            strcpy(device_admin_password_hash, password_hash);
            free(password_hash);
            DEBUG_DEVICE("admin password set");
        }
        else if (!strcmp(key, "normal_password")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD_VALUE(key);
            }
            
            char *password = json_str_get(child);
            char *password_hash = sha256_hex(password);
            strcpy(device_normal_password_hash, password_hash);
            free(password_hash);
            DEBUG_DEVICE("normal password set");
        }
        else if (!strcmp(key, "viewonly_password")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD_VALUE(key);
            }
            
            char *password = json_str_get(child);
            char *password_hash = sha256_hex(password);
            strcpy(device_viewonly_password_hash, password_hash);
            free(password_hash);
            DEBUG_DEVICE("view-only password set");
        }
        else if (!strcmp(key, "frequency")) {
            if (json_get_type(child) != JSON_TYPE_INT) {
                return INVALID_FIELD_VALUE(key);
            }

            int frequency = json_int_get(child);
            if (!validate_num(frequency, UNDEFINED, UNDEFINED, TRUE, 0, frequency_choices)) {
                return INVALID_FIELD_VALUE(key);
            }

            system_update_cpu_freq(frequency);
            DEBUG_DEVICE("CPU frequency set to %d MHz", frequency);
        }
        else if (!strcmp(key, "network_ip")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD_VALUE(key);
            }

            char *ip_config_str = json_str_get(child);
            if (ip_config_str[0]) { /* static IP */
                uint8 config[13];
                if (!validate_str_ip(ip_config_str, config, 13)) {
                    return INVALID_FIELD_VALUE(key);
                }

                if (config[4] > 32) {
                    return INVALID_FIELD_VALUE(key);
                }

                ip_addr_t ip, gw, dns;
                IP4_ADDR(&ip, config[0], config[1], config[2], config[3]);
                IP4_ADDR(&gw, config[5], config[6], config[7], config[8]);
                IP4_ADDR(&dns, config[9], config[10], config[11], config[12]);

                wifi_set_ip(&ip, config[4], &gw, &dns);

                needs_reset = TRUE;
            }
            else { /* DHCP */
                wifi_set_ip(NULL, 0, NULL, NULL);
                needs_reset = TRUE;
            }
        }
        else if (!strcmp(key, "network_wifi")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD_VALUE(key);
            }

            char *wifi_config_str = json_str_get(child);
            if (!wifi_config_str[0]) {
                return INVALID_FIELD_VALUE(key);  /* don't allow disabling wifi */
            }

            char ssid[WIFI_SSID_MAX_LEN];
            char psk[WIFI_PSK_MAX_LEN];
            uint8 bssid[WIFI_BSSID_LEN];
            if (!validate_str_wifi(wifi_config_str, ssid, psk, bssid)) {
                return INVALID_FIELD_VALUE(key);
            }

            int new_ssid  = strncmp(wifi_get_ssid(), ssid, WIFI_SSID_MAX_LEN);
            int new_psk   = strncmp(wifi_get_psk(), psk, WIFI_PSK_MAX_LEN);
            int new_bssid = memcmp(wifi_get_bssid(), bssid, WIFI_BSSID_LEN);

            if (new_ssid || new_psk || new_bssid) {
                wifi_set_ssid_psk(ssid, bssid, psk);
                needs_reset = TRUE;
            }
        }
        else if (!strcmp(key, "network_scan")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD_VALUE(key);
            }

            char *scan_str = json_str_get(child);
            if (!scan_str[0]) {
                wifi_set_scan_interval(0);
                wifi_set_scan_threshold(0);
            }
            else {
                int scan_interval, scan_threshold;
                if (!validate_str_network_scan(scan_str, &scan_interval, &scan_threshold)) {
                    return INVALID_FIELD_VALUE(key);
                }

                if (!validate_num(scan_interval, WIFI_SCAN_INTERVAL_MIN, WIFI_SCAN_INTERVAL_MAX, /* integer = */ TRUE,
                                  /* step = */ 0, /* choices = */ NULL)) {
                    return INVALID_FIELD_VALUE(key);
                }

                if (!validate_num(scan_threshold, WIFI_SCAN_THRESH_MIN, WIFI_SCAN_THRESH_MAX, /* integer = */ TRUE,
                                  /* step = */ 0, /* choices = */ NULL)) {
                    return INVALID_FIELD_VALUE(key);
                }

                wifi_set_scan_interval(scan_interval);
                wifi_set_scan_threshold(scan_threshold);
            }
        }
        else if (!strcmp(key, "ping_watchdog_interval")) {
            if (json_get_type(child) != JSON_TYPE_INT) {
                return INVALID_FIELD_VALUE(key);
            }

            int interval = json_int_get(child);
            if (!validate_num(interval, /* min = */ UNDEFINED, /* max = */ UNDEFINED, /* integer = */ TRUE,
                              /* step = */ 0, ping_watchdog_interval_choices)) {
                return INVALID_FIELD_VALUE(key);
            }

            if (interval != ping_wdt_get_interval()) {
                if (interval) {
                    ping_wdt_start(interval);
                }
                else {
                    ping_wdt_stop();
                }
            }
        }
#ifdef _SLEEP
        else if (!strcmp(key, "sleep_mode")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD_VALUE(key);
            }

            char *sleep_mode_str = json_str_get(child);
            if (sleep_mode_str[0]) {
                int wake_interval;
                int wake_duration;
                if (!validate_str_sleep_mode(sleep_mode_str, &wake_interval, &wake_duration)) {
                    return INVALID_FIELD_VALUE(key);
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
                return INVALID_FIELD_VALUE(key);
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
#endif
        else if (!strcmp(key, "config_model") && device_config_model_choices[0]) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD_VALUE(key);
            }

            char *model = json_str_get(child);
            if (!validate_str(model, device_config_model_choices)) {
                return INVALID_FIELD_VALUE(key);
            }

            strncpy(device_config_model, model, API_MAX_DEVICE_CONFIG_MODEL_LEN);
            device_config_model[API_MAX_DEVICE_CONFIG_MODEL_LEN - 1] = 0;

            DEBUG_DEVICE("config model set to %s", device_config_model);
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
                 !strcmp(key, "free_mem") ||
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

    config_save();
    
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
    
    /* add a device change event */
    event_push_device_update();

    return response_json;
}

json_t *post_reset(json_t *query_json, json_t *request_json, int *code) {
    json_t *response_json = json_obj_new();

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(API_ACCESS_LEVEL_ADMIN);
    }

    if (json_get_type(request_json) != JSON_TYPE_OBJ) {
        return API_ERROR(400, "invalid request");
    }

    json_t *factory_json = json_obj_lookup_key(request_json, "factory");
    bool factory = FALSE;
    if (factory_json) {
        if (json_get_type(factory_json) != JSON_TYPE_BOOL) {
            return INVALID_FIELD_VALUE("factory");
        }
        factory = json_bool_get(factory_json);
    }

    if (factory) {
        flashcfg_reset();
    }

    *code = 204;

    system_reset(/* delayed = */ TRUE);
    
    return response_json;
}

#ifdef _OTA

json_t *get_firmware(json_t *query_json, int *code) {
    json_t *response_json = NULL;

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(API_ACCESS_LEVEL_ADMIN);
    }

    int ota_state = ota_current_state();

    if (ota_state == OTA_STATE_IDLE) {
        bool beta = version_is_beta() || version_is_alpha();
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
    else {  /* ota busy */
        char *ota_state_str = ota_states_str[ota_state];

        response_json = json_obj_new();
        json_obj_append(response_json, "version", json_str_new(FW_VERSION));
        json_obj_append(response_json, "status", json_str_new(ota_state_str));
    }

    return response_json;
}

json_t *patch_firmware(json_t *query_json, json_t *request_json, int *code) {
    json_t *response_json = NULL;

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(API_ACCESS_LEVEL_ADMIN);
    }

    if (json_get_type(request_json) != JSON_TYPE_OBJ) {
        return API_ERROR(400, "invalid request");
    }
    
    json_t *version_json = json_obj_lookup_key(request_json, "version");
    json_t *url_json = json_obj_lookup_key(request_json, "url");
    if (!version_json && !url_json) {
        return MISSING_FIELD("version");
    }

    if (url_json) {  /* URL given */
        if (json_get_type(url_json) != JSON_TYPE_STR) {
            return INVALID_FIELD_VALUE("url");
        }

        ota_perform_url(json_str_get(url_json), on_ota_perform);
    }
    else { /* assuming version_json */
        if (json_get_type(version_json) != JSON_TYPE_STR) {
            return INVALID_FIELD_VALUE("version");
        }

        ota_perform_version(json_str_get(version_json), on_ota_perform);
    }

    return NULL;
}

#endif /* _OTA */

json_t *get_access(json_t *query_json, int *code) {
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

json_t *get_ports(json_t *query_json, int *code) {
    json_t *response_json = json_list_new();

    if (api_access_level < API_ACCESS_LEVEL_VIEWONLY) {
        return FORBIDDEN(API_ACCESS_LEVEL_VIEWONLY);
    }

    port_t **port = all_ports, *p;
    while ((p = *port++)) {
        DEBUG_API("returning attributes of port %s", p->id);
        json_list_append(response_json, port_to_json(p));
    }

    *code = 200;

    return response_json;
}

json_t *post_ports(json_t *query_json, json_t *request_json, int *code) {
    json_t *response_json = json_obj_new();

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(API_ACCESS_LEVEL_ADMIN);
    }

    if (json_get_type(request_json) != JSON_TYPE_OBJ) {
        return API_ERROR(400, "invalid request");
    }

    if (virtual_find_unused_slot(/* occupy = */ FALSE) < 0) {
        DEBUG_API("adding virtual port: no more free slots");
        return API_ERROR(400, "too many ports");
    }

    int i;
    json_t *child, *c, *c2;
    port_t *new_port = zalloc(sizeof(port_t));
    char *choice;

    /* will automatically allocate slot */
    new_port->slot = -1;

    /* id */
    child = json_obj_lookup_key(request_json, "id");
    if (!child) {
        free(new_port);
        return MISSING_FIELD("id");
    }
    if (json_get_type(child) != JSON_TYPE_STR) {
        free(new_port);
        return INVALID_FIELD_VALUE("id");
    }
    if (!validate_id(json_str_get(child))) {
        free(new_port);
        return INVALID_FIELD_VALUE("id");
    }
    if (port_find_by_id(json_str_get(child))) {
        free(new_port);
        return API_ERROR(400, "duplicate port");
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
        return INVALID_FIELD_VALUE("type");
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
        return INVALID_FIELD_VALUE("type");
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
            return INVALID_FIELD_VALUE("min");
        }

        DEBUG_API("adding virtual port: min = %d", (int) new_port->min);
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
            return INVALID_FIELD_VALUE("max");
        }

        /* min must be <= max */
        if (!IS_UNDEFINED(new_port->min) && new_port->min > new_port->max) {
            free(new_port);
            return INVALID_FIELD_VALUE("max");
        }

        DEBUG_API("adding virtual port: max = %d", (int) new_port->max);
    }
    else {
        new_port->max = UNDEFINED;
    }

    /* integer */
    child = json_obj_lookup_key(request_json, "integer");
    if (child) {
        if (json_get_type(child) != JSON_TYPE_BOOL) {
            free(new_port);
            return INVALID_FIELD_VALUE("integer");
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
            return INVALID_FIELD_VALUE("step");
        }

        DEBUG_API("adding virtual port: step = %d", (int) new_port->step);
    }
    else {
        new_port->step = UNDEFINED;
    }

    /* choices */
    child = json_obj_lookup_key(request_json, "choices");
    if (child && new_port->type == PORT_TYPE_NUMBER) {
        if (json_get_type(child) != JSON_TYPE_LIST) {
            free(new_port);
            return INVALID_FIELD_VALUE("choices");
        }

        int len = json_list_get_len(child);
        if (len < 1 || len > 256) {
            free(new_port);
            return INVALID_FIELD_VALUE("choices");
        }

        new_port->choices = zalloc((json_list_get_len(child) + 1) * sizeof(char *));
        for (i = 0; i < len; i++) {
            c = json_list_value_at(child, i);
            if (json_get_type(c) != JSON_TYPE_OBJ) {
                free_choices(new_port->choices);
                free(new_port);
                return INVALID_FIELD_VALUE("choices");
            }

            /* value */
            c2 = json_obj_lookup_key(c, "value");
            if (!c2) {
                free_choices(new_port->choices);
                free(new_port);
                return INVALID_FIELD_VALUE("choices");
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
                return INVALID_FIELD_VALUE("choices");
            }

            /* display name */
            c2 = json_obj_lookup_key(c, "display_name");
            if (c2) {
                if (json_get_type(c2) != JSON_TYPE_STR) {
                    free_choices(new_port->choices);
                    free(new_port);
                    return INVALID_FIELD_VALUE("choices");
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

    if (!virtual_port_register(new_port)) {
        return API_ERROR(500, "port registration failed");
    }

    port_mark_for_saving(new_port);
    event_push_port_add(new_port);

    *code = 201;

    return port_to_json(new_port);
}

json_t *patch_port(port_t *port, json_t *query_json, json_t *request_json, int *code) {
    DEBUG_API("updating attributes of port %s", port->id);
    
    json_t *response_json = json_obj_new();

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(API_ACCESS_LEVEL_ADMIN);
    }

    if (json_get_type(request_json) != JSON_TYPE_OBJ) {
        return API_ERROR(400, "invalid request");
    }

    int i;
    char *key;
    json_t *child;

    /* handle enabled attribute first */
    key = "enabled";
    child = json_obj_pop_key(request_json, key);
    if (child) {
        if (json_get_type(child) != JSON_TYPE_BOOL) {
            json_free(child);
            return INVALID_FIELD_VALUE(key);
        }

        if (json_bool_get(child) && !IS_ENABLED(port)) {
            port_enable(port);
        }
        else if (!json_bool_get(child) && IS_ENABLED(port)) {
            port_disable(port);
        }

        json_free(child);
    }

    /* then handle port-specific attributes */
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
                            return INVALID_FIELD_VALUE(key);
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
                            return INVALID_FIELD_VALUE(key);
                        }

                        double value = json_get_type(child) == JSON_TYPE_INT ?
                                       json_int_get(child) : json_double_get(child);
                        int idx = validate_num(value, a->min, a->max, a->integer, a->step, a->choices);
                        if (!idx) {
                            json_free(child);
                            return INVALID_FIELD_VALUE(key);
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

                        DEBUG_PORT(port, "%s set to %d", a->name, !!value);

                        break;
                    }

                    case ATTR_TYPE_STRING: {
                        if (json_get_type(child) != JSON_TYPE_STR) {
                            json_free(child);
                            return INVALID_FIELD_VALUE(key);
                        }

                        char *value = json_str_get(child);
                        int idx = validate_str(value, a->choices);
                        if (!idx) {
                            json_free(child);
                            return INVALID_FIELD_VALUE(key);
                        }

                        if (a->choices) {
                            ((int_setter_t) a->set)(port, a, idx - 1);
                        }
                        else {
                            ((str_setter_t) a->set)(port, a, value);
                        }

                        DEBUG_PORT(port, "%s set to %s", a->name, value);

                        break;
                    }
                }

                json_free(child);
            }
        }
    }

    /* finally, handle common attributes */
    for (i = 0; i < json_obj_get_len(request_json); i++) {
        key = json_obj_key_at(request_json, i);
        child = json_obj_value_at(request_json, i);

        if (!strcmp(key, "display_name")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD_VALUE(key);
            }

            free(port->display_name);
            port->display_name = NULL;
            if (*json_str_get(child)) {
                port->display_name = strndup(json_str_get(child), PORT_MAX_DISP_NAME_LEN);
            }

            DEBUG_PORT(port, "display_name set to \"%s\"", port->display_name);
        }
        else if (!strcmp(key, "unit")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD_VALUE(key);
            }

            free(port->unit);
            port->unit = NULL;
            if (*json_str_get(child)) {
                port->unit = strndup(json_str_get(child), PORT_MAX_UNIT_LEN);
            }

            DEBUG_PORT(port, "unit set to \"%s\"", port->unit);
        }
        else if (IS_OUTPUT(port) && !strcmp(key, "expression")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD_VALUE(key);
            }

            if (port->expr) {
                port_expr_remove(port);
            }

            if (port->sequence_pos >= 0) {
                port_sequence_cancel(port);
            }

            char *sexpr = json_str_get(child);
            while (isspace((int) *sexpr)) {
                sexpr++;
            }
            if (*sexpr) {
                /* parse & validate expression */
                if (strlen(sexpr) > API_MAX_EXPR_LEN) {
                    return INVALID_FIELD_VALUE(key);
                }

                expr_t *expr = expr_parse(sexpr, strlen(sexpr));
                if (!expr) {
                    return INVALID_FIELD_VALUE(key);
                }

                if (expr_check_loops(expr, port) > 1) {
                    DEBUG_API("loop detected in expression %s", sexpr);
                    expr_free(expr);
                    return INVALID_FIELD_VALUE(key);
                }

                port->sexpr = strdup(sexpr);
                port->expr = expr;

                port_rebuild_change_dep_mask(port);

                DEBUG_PORT(port, "expression set to \"%s\"", port->sexpr);
            }

            update_expressions();
        }
        else if (IS_OUTPUT(port) && !strcmp(key, "transform_write")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD_VALUE(key);
            }

            if (port->transform_write) {
                DEBUG_PORT(port, "removing write transform");
                expr_free(port->transform_write);
                free(port->stransform_write);
                port->transform_write = NULL;
                port->stransform_write = NULL;
            }

            char *stransform_write = json_str_get(child);
            while (isspace((int) *stransform_write)) {
                stransform_write++;
            }
            if (*stransform_write) {
                /* parse & validate expression */
                if (strlen(stransform_write) > API_MAX_EXPR_LEN) {
                    return INVALID_FIELD_VALUE(key);
                }

                expr_t *transform_write = expr_parse(stransform_write, strlen(stransform_write));
                if (!transform_write) {
                    return INVALID_FIELD_VALUE(key);
                }

                port_t **_ports, **ports, *p;
                bool other_deps = FALSE;
                _ports = ports = expr_port_deps(transform_write);
                if (ports) {
                    while ((p = *ports++)) {
                        if (p == port) {
                            continue;
                        }
                        other_deps = TRUE;
                        break;
                    }
                    free(_ports);
                }

                if (other_deps) {
                    DEBUG_PORT(port, "transform expression depends on other ports");
                    expr_free(transform_write);
                    return INVALID_FIELD_VALUE(key);
                }

                port->stransform_write = strdup(stransform_write);
                port->transform_write = transform_write;

                DEBUG_PORT(port, "write transform set to \"%s\"", port->stransform_write);
            }
        }
        else if (!strcmp(key, "transform_read")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD_VALUE(key);
            }

            if (port->transform_read) {
                DEBUG_PORT(port, "removing read transform");
                expr_free(port->transform_read);
                free(port->stransform_read);
                port->transform_read = NULL;
                port->stransform_read = NULL;
            }

            char *stransform_read = json_str_get(child);
            while (isspace((int) *stransform_read)) {
                stransform_read++;
            }
            if (*stransform_read) {
                /* parse & validate expression */
                if (strlen(stransform_read) > API_MAX_EXPR_LEN) {
                    return INVALID_FIELD_VALUE(key);
                }

                expr_t *transform_read = expr_parse(stransform_read, strlen(stransform_read));
                if (!transform_read) {
                    return INVALID_FIELD_VALUE(key);
                }

                port_t **_ports, **ports, *p;
                bool other_deps = FALSE;
                _ports = ports = expr_port_deps(transform_read);
                if (ports) {
                    while ((p = *ports++)) {
                        if (p == port) {
                            continue;
                        }
                        other_deps = TRUE;
                        break;
                    }
                    free(_ports);
                }

                if (other_deps) {
                    DEBUG_PORT(port, "transform expression depends on other ports");
                    expr_free(transform_read);
                    return INVALID_FIELD_VALUE(key);
                }

                port->stransform_read = strdup(stransform_read);
                port->transform_read = transform_read;

                DEBUG_PORT(port, "read transform set to \"%s\"", port->stransform_read);
            }
        }
        else if (!IS_OUTPUT(port) && (port->type == PORT_TYPE_NUMBER) && !strcmp(key, "filter")) {
            if (json_get_type(child) != JSON_TYPE_STR) {
                return INVALID_FIELD_VALUE(key);
            }

            char *filter = json_str_get(child);
            int idx;
            if (!(idx = validate_str(filter, filter_choices))) {
                return INVALID_FIELD_VALUE(key);
            }

            port_filter_set(port, (idx - 1) << 4, port->filter_width);

            DEBUG_PORT(port, "filter set to %s", filter);
        }
        else if (!IS_OUTPUT(port) && (port->type == PORT_TYPE_NUMBER) && !strcmp(key, "filter_width")) {
            if (json_get_type(child) != JSON_TYPE_INT) {
                return INVALID_FIELD_VALUE(key);
            }

            int filter_width = json_int_get(child);
            if (!validate_num(filter_width, 2, PORT_MAX_FILTER_WIDTH, TRUE, 0, NULL)) {
                return INVALID_FIELD_VALUE(key);
            }

            port_filter_set(port, FILTER_TYPE(port), filter_width);

            DEBUG_PORT(port, "filter width set to %d", filter_width);
        }
        else if (!IS_OUTPUT(port) && (port->type == PORT_TYPE_BOOLEAN) && !strcmp(key, "debounce")) {
            if (json_get_type(child) != JSON_TYPE_INT) {
                return INVALID_FIELD_VALUE(key);
            }

            int debounce = json_int_get(child);
            if (!validate_num(debounce, 0, PORT_MAX_DEBOUNCE_TIME, TRUE, 0, NULL)) {
                return INVALID_FIELD_VALUE(key);
            }

            if (debounce) {
                uint32 sampling_interval = MAX(1, port->sampling_interval);
                uint32 filter_width = debounce / sampling_interval;
                filter_width = MAX(1, MIN(PORT_MAX_FILTER_WIDTH, filter_width));
                port_filter_set(port, FILTER_TYPE_AVERAGE, filter_width);
            }
            else {
                port_filter_set(port, FILTER_TYPE_NONE, 0);
            }

            DEBUG_PORT(port, "debounce time set to %d", debounce);
            DEBUG_PORT(port, "filter width set to %d", port->filter_width);
        }
        else if (!strcmp(key, "persisted")) {
            if (json_get_type(child) != JSON_TYPE_BOOL) {
                return INVALID_FIELD_VALUE(key);
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
        else if (!IS_VIRTUAL(port) && !strcmp(key, "sampling_interval")) {
            if (json_get_type(child) != JSON_TYPE_INT) {
                return INVALID_FIELD_VALUE(key);
            }

            int sampling_interval = json_int_get(child);
            if (!validate_num(sampling_interval,
                              port->min_sampling_interval,
                              port->max_sampling_interval, TRUE, 0, NULL)) {

                return INVALID_FIELD_VALUE(key);
            }

            /* adjust debounce factor which depends on sampling interval */
            if (port->type == PORT_TYPE_BOOLEAN && FILTER_TYPE(port)) {
                sampling_interval = MAX(1, sampling_interval);
                uint32 debounce = port->filter_width * port->sampling_interval;
                uint32 filter_width = debounce / sampling_interval;
                filter_width = MAX(1, MIN(PORT_MAX_FILTER_WIDTH, filter_width));
                port_filter_set(port, FILTER_TYPE_AVERAGE, filter_width);

                DEBUG_PORT(port, "debounce time set to %d", debounce);
                DEBUG_PORT(port, "filter width set to %d", port->filter_width);
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

        /* set the actual port output value,
         * as the port might have just become an output port */
        if (IS_OUTPUT(port) && !IS_UNDEFINED(port->value)) {
            port_set_value(port, port->value);
        }
    }

    /* persist configuration */
    config_save();

    *code = 204;

    event_push_port_update(port);

    return response_json;
}

json_t *delete_port(port_t *port, json_t *query_json, int *code) {
    DEBUG_API("deleting virtual port %s", port->id);

    json_t *response_json = json_obj_new();

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(API_ACCESS_LEVEL_ADMIN);
    }

    if (!IS_VIRTUAL(port)) {
        return API_ERROR(400, "port not removable");  /* can't unregister a non-virtual port */
    }

    event_push_port_remove(port->id);

    /* free choices */
    if (port->choices) {
        free_choices(port->choices);
        port->choices = NULL;
    }

    /* removing virtual flag disables virtual port */
    port->flags &= ~PORT_FLAG_VIRTUAL_ACTIVE;

    /* we can't use port_mark_for_saving() here; the port is going to be destroyed immediately,
     * and we must save the configuration right away */
    config_save();

    if (!virtual_port_unregister(port)) {
        return API_ERROR(500, "port unregister failed");
    }

    free(port);

    *code = 204;

    return response_json;
}

json_t *get_port_value(port_t *port, json_t *query_json, int *code) {
    json_t *response_json = NULL;

    if (api_access_level < API_ACCESS_LEVEL_VIEWONLY) {
        return FORBIDDEN(API_ACCESS_LEVEL_VIEWONLY);
    }

    response_json = port_get_json_value(port);

    *code = 200;

    return response_json;
}

json_t *patch_port_value(port_t *port, json_t *query_json, json_t *request_json, int *code) {
    json_t *response_json = json_obj_new();

    if (api_access_level < API_ACCESS_LEVEL_NORMAL) {
        return FORBIDDEN(API_ACCESS_LEVEL_NORMAL);
    }

    if (!IS_ENABLED(port)) {
        return API_ERROR(400, "port disabled");
    }

    if (!IS_OUTPUT(port)) {
        return API_ERROR(400, "read-only port");
    }

    if (port->type == PORT_TYPE_BOOLEAN) {
        if (json_get_type(request_json) != JSON_TYPE_BOOL) {
            return API_ERROR(400, "invalid value");
        }

        if (!port_set_value(port, json_bool_get(request_json))) {
            return API_ERROR(400, "invalid value");
        }
    }
    else if (port->type == PORT_TYPE_NUMBER) {
        if (json_get_type(request_json) != JSON_TYPE_INT &&
            (json_get_type(request_json) != JSON_TYPE_DOUBLE || port->integer)) {

            return API_ERROR(400, "invalid value");
        }

        double value = json_get_type(request_json) == JSON_TYPE_INT ?
                       json_int_get(request_json) : json_double_get(request_json);

        if (!validate_num(value, port->min, port->max, port->integer, port->step, port->choices)) {
            return API_ERROR(400, "invalid value");
        }

        if (!port_set_value(port, value)) {
            return API_ERROR(400, "invalid value");
        }
    }

    if (IS_PERSISTED(port)) {
        port_mark_for_saving(port);
    }

    *code = 204;
    
    return response_json;
}

json_t *post_port_sequence(port_t *port, json_t *query_json, json_t *request_json, int *code) {
    json_t *response_json = NULL;

    if (api_access_level < API_ACCESS_LEVEL_NORMAL) {
        return FORBIDDEN(API_ACCESS_LEVEL_NORMAL);
    }

    if (!IS_ENABLED(port)) {
        return API_ERROR(400, "port disabled");
    }

    if (!IS_OUTPUT(port)) {
        return API_ERROR(400, "read-only port");
    }

    if (port->expr) {
        return API_ERROR(400, "port with expression");
    }

    json_t *values_json = json_obj_lookup_key(request_json, "values");
    if (!values_json) {
        return MISSING_FIELD("values");
    }
    if (json_get_type(values_json) != JSON_TYPE_LIST || !json_list_get_len(values_json)) {
        return INVALID_FIELD_VALUE("values");
    }    

    json_t *delays_json = json_obj_lookup_key(request_json, "delays");
    if (!delays_json) {
        return MISSING_FIELD("delays");
    }
    if (json_get_type(delays_json) != JSON_TYPE_LIST || !json_list_get_len(delays_json)) {
        return INVALID_FIELD_VALUE("delays");
    }
    if (json_list_get_len(delays_json) != json_list_get_len(values_json)) {
        return INVALID_FIELD_VALUE("delays");
    }

    json_t *repeat_json = json_obj_lookup_key(request_json, "repeat");
    if (!repeat_json) {
        return MISSING_FIELD("repeat");
    }
    if (json_get_type(repeat_json) != JSON_TYPE_INT) {
        return INVALID_FIELD_VALUE("repeat");
    }
    int repeat = json_int_get(repeat_json);
    if (repeat < API_MIN_SEQUENCE_REPEAT || repeat > API_MAX_SEQUENCE_REPEAT) {
        return INVALID_FIELD_VALUE("repeat");
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

    /* values */
    for (i = 0; i < json_list_get_len(values_json); i++) {
        j = json_list_value_at(values_json, i);
        if (port->type == PORT_TYPE_BOOLEAN) {
            if (json_get_type(j) != JSON_TYPE_BOOL) {
                return SEQ_INVALID_FIELD_VALUE("values");
            }

            port->sequence_values[i] = json_bool_get(j);
        }
        if (port->type == PORT_TYPE_NUMBER) {
            if (json_get_type(j) != JSON_TYPE_INT &&
                (json_get_type(j) != JSON_TYPE_DOUBLE || port->integer)) {

                return SEQ_INVALID_FIELD_VALUE("values");
            }

            double value = json_get_type(j) == JSON_TYPE_INT ? json_int_get(j) : json_double_get(j);
            if (!validate_num(value, port->min, port->max, port->integer, port->step, port->choices)) {
                return SEQ_INVALID_FIELD_VALUE("values");
            }

            port->sequence_values[i] = value;
        }
    }

    /* delays */
    for (i = 0; i < json_list_get_len(delays_json); i++) {
        j = json_list_value_at(delays_json, i);
        if (json_get_type(j) != JSON_TYPE_INT) {
            return SEQ_INVALID_FIELD_VALUE("delays");
        }
        
        port->sequence_delays[i] = json_int_get(j);
        
        if (port->sequence_delays[i] < API_MIN_SEQUENCE_DELAY || port->sequence_delays[i] > API_MAX_SEQUENCE_DELAY) {
            return SEQ_INVALID_FIELD_VALUE("delays");
        }
    }
    
    /* start sequence timer */
    os_timer_disarm(&port->sequence_timer);
    os_timer_setfn(&port->sequence_timer, on_sequence_timer, port);
    os_timer_arm(&port->sequence_timer, 1, /* repeat = */ FALSE);

    response_json = json_obj_new();
    *code = 204;
    
    return response_json;
}

json_t *get_webhooks(json_t *query_json, int *code) {
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
        if (webhooks_events_mask & (1 << e)) {
            json_list_append(json_events, json_str_new(EVENT_TYPES_STR[e]));
        }
    }

    json_obj_append(response_json, "events", json_events);
    json_obj_append(response_json, "timeout", json_int_new(webhooks_timeout));
    json_obj_append(response_json, "retries", json_int_new(webhooks_retries));

    *code = 200;

    return response_json;
}

json_t *patch_webhooks(json_t *query_json, json_t *request_json, int *code) {
    DEBUG_API("updating webhooks parameters");

    json_t *response_json = json_obj_new();

    if (api_access_level < API_ACCESS_LEVEL_ADMIN) {
        return FORBIDDEN(API_ACCESS_LEVEL_ADMIN);
    }

    if (json_get_type(request_json) != JSON_TYPE_OBJ) {
        return API_ERROR(400, "invalid request");
    }

    /* scheme */
    json_t *scheme_json = json_obj_lookup_key(request_json, "scheme");
    if (scheme_json) {
        if (json_get_type(scheme_json) != JSON_TYPE_STR) {
            return INVALID_FIELD_VALUE("scheme");
        }

#ifdef _SSL
        if (!strcmp(json_str_get(scheme_json), "https")) {
            device_flags |= DEVICE_FLAG_WEBHOOKS_HTTPS;
            DEBUG_WEBHOOKS("scheme set to https");
        }
        else
#endif
        if (!strcmp(json_str_get(scheme_json), "http")) {
            DEBUG_WEBHOOKS("scheme set to http");
            device_flags &= ~DEVICE_FLAG_WEBHOOKS_HTTPS;
        }
        else {
            return INVALID_FIELD_VALUE("scheme");
        }
    }

    /* host */
    json_t *host_json = json_obj_lookup_key(request_json, "host");
    if (host_json) {
        if (json_get_type(host_json) != JSON_TYPE_STR) {
            return INVALID_FIELD_VALUE("host");
        }
        if (strlen(json_str_get(host_json)) == 0) {
            return INVALID_FIELD_VALUE("host");
        }

        if (webhooks_host) {
            free(webhooks_host);
        }

        webhooks_host = strdup(json_str_get(host_json));
        DEBUG_WEBHOOKS("host set to \"%s\"", webhooks_host);
    }

    /* port */
    json_t *port_json = json_obj_lookup_key(request_json, "port");
    if (port_json) {
        if (json_get_type(port_json) != JSON_TYPE_INT) {
            return INVALID_FIELD_VALUE("port");
        }

        if (json_int_get(port_json) < 0 || json_int_get(port_json) > 65535) {
            return INVALID_FIELD_VALUE("port");
        }

        webhooks_port = json_int_get(port_json);
        DEBUG_WEBHOOKS("port set to %d", webhooks_port);
    }

    /* path */
    json_t *path_json = json_obj_lookup_key(request_json, "path");
    if (path_json) {
        if (json_get_type(path_json) != JSON_TYPE_STR) {
            return INVALID_FIELD_VALUE("path");
        }
        if (strlen(json_str_get(path_json)) == 0) {
            return INVALID_FIELD_VALUE("path");
        }

        if (webhooks_path) {
            free(webhooks_path);
        }

        webhooks_path = strdup(json_str_get(path_json));
        DEBUG_WEBHOOKS("path set to \"%s\"", webhooks_path);
    }

    /* password */
    json_t *password_json = json_obj_lookup_key(request_json, "password");
    if (password_json) {
        if (json_get_type(password_json) != JSON_TYPE_STR) {
            return INVALID_FIELD_VALUE("password");
        }

        char *password = json_str_get(password_json);
        char *password_hash = sha256_hex(password);
        strcpy(webhooks_password_hash, password_hash);
        free(password_hash);
        DEBUG_WEBHOOKS("password set");
    }

    /* events */
    json_t *event_json, *events_json = json_obj_lookup_key(request_json, "events");
    if (events_json) {
        if (json_get_type(events_json) != JSON_TYPE_LIST) {
            return INVALID_FIELD_VALUE("events");
        }

        int i, e, events_mask = 0, len = json_list_get_len(events_json);
        for (i = 0; i < len; i++) {
            event_json = json_list_value_at(events_json, i);
            if (json_get_type(event_json) != JSON_TYPE_STR) {
                return INVALID_FIELD_VALUE("events");
            }

            for (e = EVENT_TYPE_MIN; e <= EVENT_TYPE_MAX; e++) {
                if (!strcmp(json_str_get(event_json), EVENT_TYPES_STR[e])) {
                    events_mask |= (1 << e);
                    break;
                }
            }

            if (e > EVENT_TYPE_MAX) {
                return INVALID_FIELD_VALUE("events");
            }

            DEBUG_WEBHOOKS("event mask includes %s", EVENT_TYPES_STR[e]);
        }

        webhooks_events_mask = events_mask;
    }

    /* timeout */
    json_t *timeout_json = json_obj_lookup_key(request_json, "timeout");
    if (timeout_json) {
        if (json_get_type(timeout_json) != JSON_TYPE_INT) {
            return INVALID_FIELD_VALUE("timeout");
        }

        if (json_int_get(timeout_json) < WEBHOOKS_MIN_TIMEOUT || json_int_get(timeout_json) > WEBHOOKS_MAX_TIMEOUT) {
            return INVALID_FIELD_VALUE("timeout");
        }

        webhooks_timeout = json_int_get(timeout_json);
        DEBUG_WEBHOOKS("timeout set to %d", webhooks_timeout);
    }

    /* retries */
    json_t *retries_json = json_obj_lookup_key(request_json, "retries");
    if (retries_json) {
        if (json_get_type(retries_json) != JSON_TYPE_INT) {
            return INVALID_FIELD_VALUE("retries");
        }

        if (json_int_get(retries_json) < WEBHOOKS_MIN_RETRIES || json_int_get(retries_json) > WEBHOOKS_MAX_RETRIES) {
            return INVALID_FIELD_VALUE("retries");
        }

        webhooks_retries = json_int_get(retries_json);
        DEBUG_WEBHOOKS("retries set to %d", webhooks_retries);
    }

    /* enabled */
    json_t *enabled_json = json_obj_lookup_key(request_json, "enabled");
    if (enabled_json) {
        if (json_get_type(enabled_json) != JSON_TYPE_BOOL) {
            return INVALID_FIELD_VALUE("enabled");
        }

        bool enabled = json_bool_get(enabled_json);
        if (enabled) {
            /* we can't enable webhooks unless we have a host and a path */
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

    config_save();

    *code = 204;

    return response_json;
}

json_t *get_wifi(json_t *query_json, int *code) {
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

json_t *port_attrdefs_to_json(port_t *port) {
    json_t *json = json_obj_new();
    json_t *attrdef_json;

    if (!IS_OUTPUT(port)) {
        if (port->type == PORT_TYPE_NUMBER) {
            attrdef_json = attrdef_to_json("Filter Type", "Configures a filter for the port value.", /* unit = */ NULL,
                                           ATTR_TYPE_STRING, /* modifiable = */ TRUE, /* min = */ UNDEFINED,
                                           /* max = */ UNDEFINED, /* integer = */ FALSE, /* step = */ 0, filter_choices,
                                           /* reconnect = */FALSE);
            json_obj_append(json, "filter", attrdef_json);

            attrdef_json = attrdef_to_json("Filter Width", "Sets the filter width.", "samples", ATTR_TYPE_NUMBER,
                                           /* modifiable = */ TRUE, /* min = */ 2, /* max = */ PORT_MAX_FILTER_WIDTH,
                                           /* integer = */ TRUE, /* step = */ 0, /* choices = */ NULL,
                                           /* reconnect */ FALSE);
            json_obj_append(json, "filter_width", attrdef_json);
        }
        else {  /* assuming PORT_TYPE_BOOLEAN */
            attrdef_json = attrdef_to_json("Debounce Time", "Sets the debounce filtering time.", "milliseconds",
                                           ATTR_TYPE_NUMBER, /* modifiable = */ TRUE, /* min = */ 0,
                                           /* max = */ PORT_MAX_DEBOUNCE_TIME, /* integer = */ TRUE, /* step = */ 0,
                                           /* choices = */ NULL, /* reconnect = */ FALSE);
            json_obj_append(json, "debounce", attrdef_json);
        }
    }

    if (!IS_VIRTUAL(port)) {
        attrdef_json = attrdef_to_json("Sampling Interval", "Indicates how often to read the port value.",
                                       "milliseconds", ATTR_TYPE_NUMBER, /* modifiable = */ TRUE,
                                       port->min_sampling_interval, port->max_sampling_interval,
                                       /* integer = */ TRUE , /* step = */ 0, /* choices = */ NULL,
                                       /* reconnect = */ FALSE);
        json_obj_append(json, "sampling_interval", attrdef_json);
    }

    if (port->attrdefs) {
        attrdef_t *a, **attrdefs = port->attrdefs;
        while ((a = *attrdefs++)) {
            json_t *attrdef_json = attrdef_to_json(a->display_name ? a->display_name : "",
                                                   a->description ? a->description : "",
                                                   a->unit ? a->unit : "", a->type, a->modifiable,
                                                   a->min, a->max, a->integer, a->step, a->choices, a->reconnect);
            json_stringify(attrdef_json);
            json_obj_append(json, a->name, attrdef_json);
        }
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

    attrdef_json = attrdef_to_json("Network RSSI", "The measured RSSI (signal strength).", "dBm", ATTR_TYPE_NUMBER,
                                   /* modifiable = */ FALSE, /* min = */ -100, /* max = */ -30, /* integer = */ TRUE,
                                   /* step = */ 0, /* choices = */ NULL, /* reconnect = */ FALSE);
    json_obj_append(json, "network_rssi", attrdef_json);

    attrdef_json = attrdef_to_json("Network BSSID", "The BSSID of the currently connected AP.", /* unit = */ NULL,
                                   ATTR_TYPE_STRING, /* modifiable = */ FALSE, /* min = */ UNDEFINED,
                                   /* max = */ UNDEFINED, /* integer = */ FALSE, /* step = */ 0, /* choices = */ NULL,
                                   /* reconnect = */ FALSE);
    json_obj_append(json, "network_bssid", attrdef_json);

    attrdef_json = attrdef_to_json("Network Scan",
                                   "Controls the WiFi scanning mode; format is <interval_seconds>:<threshold_dBm>. "
                                   "Interval is between 1 and 3600 seconds; threshold is between 1 and 50 dBm. "
                                   "Empty string disables scanning mode.", /* unit = */ NULL,
                                   ATTR_TYPE_STRING, /* modifiable = */ TRUE, /* min = */ UNDEFINED,
                                   /* max = */ UNDEFINED, /* integer = */ FALSE, /* step = */ 0, /* choices = */ NULL,
                                   /* reconnect = */ FALSE);
    json_obj_append(json, "network_scan", attrdef_json);

    attrdef_json = attrdef_to_json("Ping Watchdog Interval",
                                   "Sends ping requests and resets the system if the gateway is not reachable after 10 "
                                   "requests (0 disables pinging).", "seconds", ATTR_TYPE_NUMBER,
                                   /* modifiable = */ TRUE, /* min = */ UNDEFINED, /* max = */ UNDEFINED,
                                   /* integer = */ TRUE, /* step = */ 0, ping_watchdog_interval_choices,
                                   /* reconnect = */ FALSE);
    json_obj_append(json, "ping_watchdog_interval", attrdef_json);

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
#endif

#ifdef _BATTERY
    attrdef_json = attrdef_to_json("Battery Voltage", "The battery voltage.", "mV", ATTR_TYPE_NUMBER,
                                   /* modifiable = */ FALSE, /* min = */ UNDEFINED, /* max = */ UNDEFINED,
                                   /* integer = */ FALSE, /* step = */ 0, /* choices = */ NULL,
                                   /* reconnect = */ FALSE);
    json_obj_append(json, "battery_voltage", attrdef_json);
#endif

    attrdef_json = attrdef_to_json("Free Memory", "The current free heap memory.", "kB", ATTR_TYPE_NUMBER,
                                   /* modifiable = */ FALSE, /* min = */ UNDEFINED, /* max = */ UNDEFINED,
                                   /* integer = */ TRUE, /* step = */ 0, /* choices = */ NULL, /* reconnect = */ FALSE);
    json_obj_append(json, "free_mem", attrdef_json);

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

    if (device_config_model_choices[0]) {
        attrdef_json = attrdef_to_json("Configuration Model", "Device configuration model.", /* unit = */ NULL,
                                       ATTR_TYPE_STRING, /* modifiable = */ TRUE, /* min = */ UNDEFINED,
                                       /* max = */ UNDEFINED, /* integer = */ FALSE, /* step = */ 0,
                                       device_config_model_choices, /* reconnect = */ FALSE);
        json_obj_append(json, "config_model", attrdef_json);
    }

#ifdef _DEBUG
    attrdef_json = attrdef_to_json("Debug", "Indicates that debugging was enabled when the firmware was built.",
                                   /* unit = */ NULL, ATTR_TYPE_BOOLEAN, /* modifiable = */ FALSE,
                                   /* min = */ UNDEFINED, /* max = */ UNDEFINED, /* integer = */ FALSE, /* step = */ 0,
                                   /* choices = */ NULL, /* reconnect = */ FALSE);
    json_obj_append(json, "debug", attrdef_json);
#endif

    return json;
}

json_t *attrdef_to_json(char *display_name, char *description, char *unit, char type, bool modifiable,
                        double min, double max, bool integer, double step, char **choices,
                        bool reconnect) {

    json_t *json = json_obj_new();

    json_obj_append(json, "display_name", json_str_new(display_name));
    json_obj_append(json, "description", json_str_new(description));
    if (unit) {
        json_obj_append(json, "unit", json_str_new(unit));
    }
    json_obj_append(json, "modifiable", json_bool_new(modifiable));

    switch (type) {
        case ATTR_TYPE_BOOLEAN:
            json_obj_append(json, "type", json_str_new(API_ATTR_TYPE_BOOLEAN));
            break;

        case ATTR_TYPE_NUMBER:
            json_obj_append(json, "type", json_str_new(API_ATTR_TYPE_NUMBER));
            break;

        case ATTR_TYPE_STRING:
            json_obj_append(json, "type", json_str_new(API_ATTR_TYPE_STRING));
            break;
    }

    if (type == ATTR_TYPE_NUMBER) {
        if (!IS_UNDEFINED(min)) {
            json_obj_append(json, "min", json_double_new(min));
        }

        if (!IS_UNDEFINED(max)) {
            json_obj_append(json, "max", json_double_new(max));
        }

        if (integer) {
            json_obj_append(json, "integer", json_bool_new(TRUE));
        }

        if (step && !IS_UNDEFINED(step)) {
            json_obj_append(json, "step", json_double_new(step));
        }
    }

    if (choices) {
        json_t *list = json_list_new();
        char *c;
        while ((c = *choices++)) {
            json_list_append(list, choice_to_json(c, type));
        }

        json_obj_append(json, "choices", list);
    }

    if (reconnect) {
        json_obj_append(json, "reconnect", json_bool_new(TRUE));
    }

    return json;
}

json_t *choice_to_json(char *choice, char type) {
    json_t *choice_json = json_obj_new();

    char *display_name = get_choice_display_name(choice);
    if (display_name) {
        json_obj_append(choice_json, "display_name", json_str_new(display_name));
    }

    if (type == ATTR_TYPE_NUMBER) /* also PORT_TYPE_NUMBER */ {
        json_obj_append(choice_json, "value", json_double_new(get_choice_value_num(choice)));
    }
    else {
        json_obj_append(choice_json, "value", json_str_new(get_choice_value_str(choice)));
    }

    return choice_json;
}

void free_choices(char **choices) {
    char *c;
    while ((c = *choices++)) {
        free(c);
    }

    free(choices);
}

bool validate_num(double value, double min, double max, bool integer, double step, char **choices) {
    if ((min != max) && ((!IS_UNDEFINED(min) && value < min) || (!IS_UNDEFINED(max) && value > max))) {
        return FALSE;
    }

    if (choices) {
        char *c;
        int i = 0;
        while ((c = *choices++)) {
            if (get_choice_value_num(c) == value) {
                return i + 1;
            }

            i++;
        }

        return FALSE;
    }

    if (integer) {
        if (ceil(value) != value) {
            return FALSE;
        }

        if (step && !IS_UNDEFINED(step)) {
            double r = (value - min) / step;
            if (ceil(r) != r) {
                return FALSE;
            }
        }
    }

    return TRUE;
}

bool validate_str(char *value, char **choices) {
    if (choices) {
        char *c;
        int i = 0;
        while ((c = *choices++)) {
            if (!strcmp(get_choice_value_str(c), value)) {
                return i + 1;
            }

            i++;
        }

        return FALSE;
    }

    return TRUE;
}

bool validate_id(char *id) {
    if (!isalpha((int) id[0]) && id[0] != '_') {
        return FALSE;
    }
    
    int c;
    while ((c = *id++)) {
        if (!isalnum(c) && c != '_') {
            return FALSE;
        }
    }
    
    return TRUE;
}

bool validate_str_ip(char *ip, uint8 *a, int len) {
    a[0] = 0;
    char *s = ip;
    int c, i = 0;
    while ((c = *s++)) {
        if (isdigit(c)) {
            a[i] = a[i] * 10 + (c - '0');
        }
        else if ((c == '.' || c == '/' || c == ':') && (i < len - 1)) {
            i++;
            a[i] = 0;
        }
        else {
            return FALSE;
        }
    }

    if (i < len - 1) {
        return FALSE;
    }

    return TRUE;
}

bool validate_str_wifi(char *wifi, char *ssid, char *psk, uint8 *bssid) {
    // TODO special treatment for escaped colons \:, which should not be considered separators
    char *s = wifi;
    int c, len;
    char t[3] = {0, 0, 0};

    memset(ssid, 0, WIFI_SSID_MAX_LEN);
    memset(psk, 0, WIFI_PSK_MAX_LEN);
    memset(bssid, 0, WIFI_BSSID_LEN);

    /* SSID */
    len = 0;
    while ((c = *s++)) {
        if (c == ':') {
            break;
        }
        else {
            if (len > WIFI_SSID_MAX_LEN) {
                return FALSE;
            }

            ssid[len++] = c;
        }
    }

    if (!c) {
        return TRUE;
    }

    /* PSK */
    len = 0;
    while ((c = *s++)) {
        if (c == ':') {
            break;
        }
        else {
            if (len > WIFI_PSK_MAX_LEN) {
                return FALSE;
            }

            psk[len++] = c;
        }
    }

    if (!c) {
        return TRUE;
    }

    /* BSSID */
    len = 0;
    while (TRUE) {
        if (len > WIFI_BSSID_LEN) {
            return FALSE;
        }

        if (!s[0]) {
            break;
        }

        if (!s[1]) {
            return FALSE;
        }

        t[0] = s[0]; t[1] = s[1];
        bssid[len++] = strtol(t, NULL, 16);

        s += 2;
    }

    if (len != 6 && len != 0) {
        return FALSE;
    }

    return TRUE;
}

bool validate_str_network_scan(char *scan, int *scan_interval, int *scan_threshold) {
    char c, *s = scan;
    int pos = 0;
    int no = 0;
    while ((c = *s++)) {
        if (c == ':') {
            if (pos >= 1) {
                return FALSE;
            }

            pos++;
            *scan_interval = no;
            no = 0;
        }
        else if (isdigit((int) c)) {
            no = no * 10 + c - '0';
        }
        else {
            return FALSE;
        }
    }

    if (pos == 1) {
        *scan_threshold = no;
    }
    else {
        return FALSE;
    }

    return TRUE;
}

#ifdef _SLEEP

bool validate_str_sleep_mode(char *sleep_mode, int *wake_interval, int *wake_duration) {
    char *s = sleep_mode;
    int c, wi = 0, wd = 0;

    while ((c = *s++)) {
        if (c == ':') {
            break;
        }
        else if (isdigit(c)) {
            wi = wi * 10 + (c - '0');
        }
        else {
            return FALSE;
        }
    }

    if (!c) {
        return FALSE;
    }

    while ((c = *s++)) {
        if (isdigit(c)) {
            wd = wd * 10 + (c - '0');
        }
        else {
            return FALSE;
        }
    }

    if (wi < SLEEP_WAKE_INTERVAL_MIN || wi > SLEEP_WAKE_INTERVAL_MAX) {
        return FALSE;
    }

    if (wd < SLEEP_WAKE_DURATION_MIN || wd > SLEEP_WAKE_DURATION_MAX) {
        return FALSE;
    }

    *wake_interval = wi;
    *wake_duration = wd;

    return TRUE;
}

#endif  /* _SLEEP */

void on_sequence_timer(void *arg) {
    port_t *port = arg;

    if (port->sequence_pos < port->sequence_len) {
        port_set_value(port, port->sequence_values[port->sequence_pos]);

        DEBUG_PORT(port, "sequence delay of %d ms", port->sequence_delays[port->sequence_pos]);

        os_timer_arm(&port->sequence_timer, port->sequence_delays[port->sequence_pos], /* repeat = */ FALSE);
        port->sequence_pos++;
    }
    else { /* sequence ended */
        if (port->sequence_repeat > 1 || port->sequence_repeat == 0) { /* must repeat */
            if (port->sequence_repeat) {
                port->sequence_repeat--;
            }

            DEBUG_PORT(port, "repeating sequence (%d remaining iterations)", port->sequence_repeat);

            port->sequence_pos = 0;
            on_sequence_timer(arg);
        }
        else { /* single iteration or repeat ended */
            os_timer_disarm(&port->sequence_timer);
            free(port->sequence_values);
            free(port->sequence_delays);
            port->sequence_pos = -1;
            port->sequence_repeat = -1;

            DEBUG_PORT(port, "sequence done");

            if (IS_PERSISTED(port)) {
                port_mark_for_saving(port);
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
    else {  /* error */
        json_t *response_json = json_obj_new();
        char error[] = "no such version";
        json_obj_append(response_json, "error", json_str_new(error));
        respond_json(api_conn, 404, response_json);
    }

    api_conn_reset();
}

#endif  /* _OTA */

void on_wifi_scan(wifi_scan_result_t *results, int len) {
    DEBUG_API("got wifi scan results");

    if (!api_conn) {
        return;  /* such is life */
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
