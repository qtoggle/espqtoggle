
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
#include "espgoodies/sleep.h"
#include "espgoodies/system.h"
#include "espgoodies/wifi.h"

#include "api.h"
#include "apiutils.h"
#include "common.h"
#include "core.h"
#include "device.h"
#include "events.h"
#include "peripherals.h"
#include "ports.h"
#include "stringpool.h"
#include "webhooks.h"
#include "config.h"


static bool provisioning = FALSE;


void ICACHE_FLASH_ATTR on_provisioning_config_response(
                           char *body,
                           int body_len,
                           int status,
                           char *header_names[],
                           char *header_values[],
                           int header_count,
                           uint8 addr[]
                       );


void config_init(void) {
    uint8 *config_data = zalloc(FLASH_CONFIG_SIZE_DEFAULT);
    flashcfg_load(FLASH_CONFIG_SLOT_DEFAULT, config_data);

    /* If config data is full of 0xFF, that usually indicates erased flash. Fill it with 0s in that case, which is what
     * we use for default config. */
    uint16 i;
    bool erased = TRUE;
    for (i = 0; i < 32; i++) {
        if (config_data[i] != 0xFF) {
            erased = FALSE;
            break;
        }
    }

    if (erased) {
        DEBUG_CONFIG("detected erased flash config");
        memset(config_data, 0, FLASH_CONFIG_SIZE_DEFAULT);
        flashcfg_save(FLASH_CONFIG_SLOT_DEFAULT, config_data);
    }

    device_load(config_data);

    if (!device_tcp_port) {
        device_tcp_port = DEFAULT_TCP_PORT;
    }

    peripherals_init(config_data);
    ports_init(config_data);

    /* At this point we no longer need the config data */
    free(config_data);

    /* Parse port value expressions */
    port_t *p;
    for (i = 0; i < all_ports_count; i++) {
        p = all_ports[i];
        if (!IS_PORT_ENABLED(p)) {
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
    uint8 *config_data = zalloc(FLASH_CONFIG_SIZE_DEFAULT);
    uint32 strings_offs = 1; /* Address 0 in strings pool represents an unset string, so it's left out */

    flashcfg_load(FLASH_CONFIG_SLOT_DEFAULT, config_data);
    peripherals_save(config_data, &strings_offs);
    ports_save(config_data, &strings_offs);
    device_save(config_data, &strings_offs);
    flashcfg_save(FLASH_CONFIG_SLOT_DEFAULT, config_data);
    free(config_data);

    DEBUG_CONFIG("total strings size is %d", strings_offs - 1);
}

void config_factory_reset(void) {
    uint8 *config_data = zalloc(FLASH_CONFIG_SIZE_DEFAULT);
    uint32 strings_offs = 1; /* Address 0 in strings pool represents an unset string, so it's left out */
    char *strings_ptr = (char *) config_data + CONFIG_OFFS_STR_BASE;

    /* Preserve configuration name */
    string_pool_write(strings_ptr, &strings_offs, device_config_name, config_data + CONFIG_OFFS_CONFIG_NAME);

    flashcfg_save(FLASH_CONFIG_SLOT_DEFAULT, config_data);
    free(config_data);
}

void config_start_auto_provisioning(bool ignore_version) {
    if (provisioning) {
        DEBUG_CONFIG("provisioning: busy");
        return;
    }

    /* No automatic configuration if:
     *  * no config_name specified
     *  * config_name starts with "custom/"
     *  * provisioning_config version is 0
     */

    if (!device_config_name[0] || !strncmp(device_config_name, "custom/", 7)) {
        DEBUG_CONFIG("provisioning: no configuration");
        return;
    }
    if (device_provisioning_version == 0 && !ignore_version) {
        DEBUG_CONFIG("provisioning: skipping due to null version");
        return;
    }

    provisioning = TRUE;

    char url[256];
    snprintf(url, sizeof(url), "%s%s/%s.json", FW_BASE_URL, FW_BASE_CFG_PATH, device_config_name);

    DEBUG_CONFIG("provisioning: fetching from \"%s\"", url);

    httpclient_request(
        "GET",
        url,
        /* body = */ NULL,
        /* body_len = */ 0,
        /* header_names = */ NULL,
        /* header_values = */ NULL,
        /* header_count = */ 0,
        on_provisioning_config_response,
        HTTP_DEF_TIMEOUT
    );
}

bool config_is_provisioning(void) {
    return provisioning;
}


bool config_apply_json_provisioning(json_t *config, bool force) {
    if (provisioning) {
        DEBUG_CONFIG("provisioning: busy");
        return FALSE;
    }

    if (json_get_type(config) != JSON_TYPE_OBJ) {
        DEBUG_CONFIG("provisioning: invalid config");
        return FALSE;
    }

    /* Verify minimum FW version requirement */
    json_t *min_fw_version_json = json_obj_pop_key(config, "min_fw_version");
    if (min_fw_version_json) {
        version_t min_fw_version;
        version_t sys_fw_version;
        char *min_fw_version_str = json_str_get(min_fw_version_json);
        json_free(min_fw_version_json);
        version_parse(min_fw_version_str, &min_fw_version);
        system_get_fw_version(&sys_fw_version);

        if (!force && version_cmp(&min_fw_version, &sys_fw_version) > 0) {
            DEBUG_CONFIG("provisioning: ignoring config due to min FW version %s", min_fw_version_str);
            return FALSE;
        }
    }

    json_t *provisioning_version_json = json_obj_pop_key(config, "version");
    uint16 provisioning_version = 0;
    if (provisioning_version_json) {
        provisioning_version = json_int_get(provisioning_version_json);
        json_free(provisioning_version_json);
    }

    if (!force && device_provisioning_version == provisioning_version) {
        DEBUG_CONFIG("provisioning: ignoring config due to same provisioning version %d", provisioning_version);
        return FALSE;
    }

    /* Setting this to TRUE here will simply prevent triggering of events during following API calls */
    provisioning = TRUE;

    DEBUG_CONFIG("provisioning: updating version to %d", provisioning_version);
    device_provisioning_version = provisioning_version;

    /* Temporarily set API access level to admin */
    api_conn_save();
    api_conn_set((void *) 1, API_ACCESS_LEVEL_ADMIN);

    bool result = TRUE;

    json_t *device_config = json_obj_pop_key(config, "device");
    if (device_config) {
        result = config_apply_device_provisioning(device_config);
        json_free(device_config);

        if (!result) {
            goto done;
        }
    }

    system_soft_wdt_feed();

    json_t *peripherals_config = json_obj_pop_key(config, "peripherals");
    if (peripherals_config) {
        result = config_apply_peripherals_provisioning(peripherals_config);
        json_free(peripherals_config);

        if (!result) {
            goto done;
        }
    }

    system_soft_wdt_feed();

    json_t *system_config = json_obj_pop_key(config, "system");
    if (system_config) {
        result = config_apply_system_provisioning(system_config);
        json_free(system_config);

        if (!result) {
            goto done;
        }
    }

    system_soft_wdt_feed();

    json_t *ports_config = json_obj_pop_key(config, "ports");
    if (ports_config) {
        result = config_apply_ports_provisioning(
            ports_config,
            /* error_response_json = */ NULL,
            /* error_port_id = */ NULL
        );
        json_free(ports_config);

        if (!result) {
            goto done;
        }
    }

done:

    api_conn_restore();
    provisioning = FALSE;

    return result;
}

bool config_apply_device_provisioning(json_t *device_config) {
    json_t *response_json;
    json_t *attr_json;
    int code;

    DEBUG_CONFIG("provisioning: applying device config");

    /* Never update device name */
    attr_json = json_obj_pop_key(device_config, "name");
    json_free(attr_json);

    /* Don't overwrite display name */
    if (device_display_name[0]) {
        json_free(json_obj_pop_key(device_config, "display_name"));
    }

    /* Never update network attributes */
    for (int i = 0; i < json_obj_get_len(device_config); i++) {
        char *key = json_obj_key_at(device_config, i);
        if (!strncmp(key, "wifi_", 5) || !strncmp(key, "ip_", 3)) {
            DEBUG_CONFIG("ignoring device attribute %s", key);
            json_free(json_obj_pop_at(device_config, i));
            i--;
        }
    }

    if (json_get_type(device_config) == JSON_TYPE_OBJ) {
        DEBUG_CONFIG("provisioning: setting device attributes");
        code = 200;
        response_json = api_patch_device(/* query_json = */ NULL, device_config, &code);
        json_free(response_json);
        if (code / 100 != 2) {
            DEBUG_CONFIG("provisioning: api_patch_device() failed with status code %d", code);
            return FALSE;
        }
    }
    else {
        DEBUG_CONFIG("provisioning: invalid device config");
        return FALSE;
    }

    return TRUE;
}

bool config_apply_peripherals_provisioning(json_t *peripherals_config) {
    json_t *response_json;
    int code;

    DEBUG_CONFIG("provisioning: applying peripherals config");

    if (json_get_type(peripherals_config) == JSON_TYPE_LIST) {
        code = 200;
        response_json = api_put_peripherals(/* query_json = */ NULL, peripherals_config, &code);
        json_free(response_json);
        if (code / 100 != 2) {
            DEBUG_CONFIG("provisioning: api_patch_peripherals() failed with status code %d", code);
            return FALSE;
        }
    }
    else {
        DEBUG_CONFIG("provisioning: invalid peripherals config");
        return FALSE;
    }

    return TRUE;
}

bool config_apply_system_provisioning(json_t *system_config) {
    json_t *response_json;
    int code;

    DEBUG_CONFIG("provisioning: applying system config");

    if (json_get_type(system_config) == JSON_TYPE_OBJ) {
        code = 200;
        response_json = api_patch_system(/* query_json = */ NULL, system_config, &code);
        json_free(response_json);
        if (code / 100 != 2) {
            DEBUG_CONFIG("provisioning: api_patch_system() failed with status code %d", code);
            return FALSE;
        }
    }
    else {
        DEBUG_CONFIG("provisioning: invalid system config");
        return FALSE;
    }

    return TRUE;
}

bool config_apply_ports_provisioning(json_t *ports_config, json_t **error_response_json, char **error_port_id) {
    json_t *port_config;
    int i;
    char *port_id;

    DEBUG_CONFIG("provisioning: applying ports config");

    if (json_get_type(ports_config) == JSON_TYPE_LIST) {
        for (i = 0; i < json_list_get_len(ports_config); i++) {
            port_config = json_list_value_at(ports_config, i);

            if (!config_apply_port_provisioning(port_config, &port_id, error_response_json)) {
                if (error_port_id) {
                    *error_port_id = port_id;
                }
                else {
                    free(port_id);
                }
                return FALSE;
            }

            free(port_id);

            system_soft_wdt_feed();
        }
    }
    else {
        DEBUG_CONFIG("provisioning: invalid ports config");
        return FALSE;
    }

    return TRUE;
}

bool config_apply_port_provisioning(json_t *port_config, char **port_id, json_t **error_response_json) {
    json_t *response_json;
    json_t *id_json;
    json_t *virtual_json;
    json_t *request_json;
    json_t *attr_json;
    json_t *value_json;
    port_t *port;
    int code;
    bool result = TRUE;

    if (port_id) {
        *port_id = NULL;
    }
    if (error_response_json) {
        *error_response_json = NULL;
    }

    if (json_get_type(port_config) == JSON_TYPE_OBJ) {
        id_json = json_obj_pop_key(port_config, "id");
        virtual_json = json_obj_pop_key(port_config, "virtual");
        if (id_json && json_get_type(id_json) == JSON_TYPE_STR) {
            char *id = json_str_get(id_json);
            if (port_id) {
                *port_id = strdup(id);
            }

            DEBUG_CONFIG("provisioning: applying port %s config", id);
            port = port_find_by_id(id);

            /* Virtual ports have to be added first */
            if (virtual_json && json_get_type(virtual_json) == JSON_TYPE_BOOL && json_bool_get(virtual_json)) {
                if (port) {
                    DEBUG_CONFIG("provisioning: virtual port %s exists, removing it first", id);
                    code = 200;
                    response_json = api_delete_port(port, /* query_json = */ NULL, &code);
                    if (code / 100 != 2) {
                        DEBUG_CONFIG("provisioning: api_delete_port() failed with status code %d", code);
                        result = FALSE;
                    }
                    if (!result && error_response_json) {
                        *error_response_json = response_json;
                    }
                    else {
                        json_free(response_json);
                    }
                }

                DEBUG_CONFIG("provisioning: adding virtual port %s", id);
                request_json = json_obj_new();
                json_obj_append(request_json, "id", json_str_new(id));

                /* Pass non-modifiable virtual port attributes from port_config to request_json */
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
                if (code / 100 != 2) {
                    DEBUG_CONFIG("provisioning: api_post_ports() failed with status code %d", code);
                    result = FALSE;
                }
                if (!result && error_response_json) {
                    *error_response_json = response_json;
                }
                else {
                    json_free(response_json);
                }

                json_free(request_json);

                /* Lookup port reference after adding it */
                port = port_find_by_id(id);
            }

            if (port) {
                value_json = json_obj_pop_key(port_config, "value");

                DEBUG_CONFIG("provisioning: setting port %s attributes", id);

                code = 200;
                response_json = api_patch_port(
                    port,
                    /* query_json = */ NULL,
                    port_config, &code,
                    /* provisioning = */ TRUE
                );
                if (code / 100 != 2) {
                    DEBUG_CONFIG("provisioning: api_patch_port() failed with status code %d", code);
                    result = FALSE;
                }
                if (!result && error_response_json) {
                    *error_response_json = response_json;
                }
                else {
                    json_free(response_json);
                }

                if (value_json) {
                    /* Value was also supplied with provisioning */

                    if (json_get_type(value_json) != JSON_TYPE_NULL &&
                        IS_PORT_ENABLED(port) &&
                        IS_PORT_WRITABLE(port)
                    ) {
                        DEBUG_CONFIG("provisioning: setting port %s value", id);

                        code = 200;
                        response_json = api_patch_port_value(port, /* query_json = */ NULL, value_json, &code);
                        if (code / 100 != 2) {
                            DEBUG_CONFIG("provisioning: api_patch_port_value() failed with status code %d", code);
                            result = FALSE;
                        }
                        if (!result && error_response_json) {
                            *error_response_json = response_json;
                        }
                        else {
                            json_free(response_json);
                        }
                    }

                    json_free(value_json);
                }
            }
            else { /* Got the id of an inexistent, non-virtual port */
                DEBUG_CONFIG("provisioning: skipping inexistent port with id %s", id);
            }
        }
        else {
            /* Missing id - not an error */
            DEBUG_CONFIG("provisioning: invalid or missing port id");
        }

        json_free(id_json);
        json_free(virtual_json);
    }
    else {
        DEBUG_CONFIG("provisioning: invalid port config");
        result = FALSE;
    }

    return result;
}

void on_provisioning_config_response(
    char *body,
    int body_len,
    int status,
    char *header_names[],
    char *header_values[],
    int header_count,
    uint8 addr[]
) {
    provisioning = FALSE;

    if (status == 200) {
        json_t *config = json_parse(body);
        if (!config) {
            DEBUG_CONFIG("provisioning: invalid JSON");
            return;
        }

        if (config_apply_json_provisioning(config, /* force = */ FALSE)) {
            event_push_full_update();
        }

        json_free(config);
    }
    else {
        DEBUG_CONFIG("provisioning: got status %d", status);
    }
}
