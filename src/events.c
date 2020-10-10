
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

#include <user_interface.h>
#include <mem.h>

#include "espgoodies/common.h"
#include "espgoodies/ota.h"

#include "api.h"
#include "common.h"
#include "config.h"
#include "device.h"
#include "sessions.h"
#include "webhooks.h"
#include "events.h"


char *EVENT_TYPES_STR[] = {
    NULL,
    "value-change",
    "port-update",
    "port-add",
    "port-remove",
    "device-update",
    "full-update"
};

int EVENT_ACCESS_LEVELS[] = {
    0, /* offset */
    API_ACCESS_LEVEL_VIEWONLY, /* value-change */
    API_ACCESS_LEVEL_VIEWONLY, /* port-update */
    API_ACCESS_LEVEL_VIEWONLY, /* port-add */
    API_ACCESS_LEVEL_VIEWONLY, /* port-remove */
    API_ACCESS_LEVEL_ADMIN,    /* device-update */
    API_ACCESS_LEVEL_VIEWONLY  /* full-update */
};


static void ICACHE_FLASH_ATTR event_push(int type, char *port_id);


void event_push_value_change(port_t *port) {
    event_push(EVENT_TYPE_VALUE_CHANGE, port->id);
}

void event_push_port_update(port_t *port) {
    event_push(EVENT_TYPE_PORT_UPDATE, port->id);
}

void event_push_port_add(port_t *port) {
    event_push(EVENT_TYPE_PORT_ADD, port->id);
}

void event_push_port_remove(port_t *port) {
    event_push(EVENT_TYPE_PORT_REMOVE, port->id);
}

void event_push_device_update(void) {
    event_push(EVENT_TYPE_DEVICE_UPDATE, NULL);
}

void event_push_full_update(void) {
    event_push(EVENT_TYPE_FULL_UPDATE, NULL);
}

json_t *event_to_json(event_t *event, json_refs_ctx_t *json_refs_ctx) {
    json_t *params = NULL;
    port_t *port;

    switch (event->type) {
        case EVENT_TYPE_VALUE_CHANGE:
            port = port_find_by_id(event->port_id);
            if (!port) {
                DEBUG_EVENTS("dropping %s event for inexistent port %s", EVENT_TYPES_STR[event->type], event->port_id);
                return NULL;
            }

            params = json_obj_new();
            json_obj_append(params, "id", json_str_new(event->port_id));
            json_obj_append(params, "value", port_get_json_value(port));
            break;

        case EVENT_TYPE_PORT_UPDATE:
        case EVENT_TYPE_PORT_ADD:
            port = port_find_by_id(event->port_id);
            if (!port) {
                DEBUG_EVENTS("dropping %s event for inexistent port %s", EVENT_TYPES_STR[event->type], event->port_id);
                return NULL;
            }

            params = port_to_json(port, json_refs_ctx);
            break;

        case EVENT_TYPE_PORT_REMOVE:
            params = json_obj_new();
            json_obj_append(params, "id", json_str_new(event->port_id));
            break;

        case EVENT_TYPE_DEVICE_UPDATE:
            params = device_to_json();
            break;

    }

    json_t *json = json_obj_new();
    json_obj_append(json, "type", json_str_new(EVENT_TYPES_STR[event->type]));
    if (params) {
        json_obj_append(json, "params", params);
    }

    return json;
}

void event_free(event_t *event) {
    free(event->port_id);
    free(event);
}

void event_push(int type, char *port_id) {
    /* Don't push any event while performing OTA */
#ifdef _OTA
    if (ota_busy()) {
        DEBUG_EVENTS("skipping %s(%s) event (OTA)", EVENT_TYPES_STR[type], port_id ? port_id : "null");
        return;
    }
#endif

    /* Don't push any event while provisioning, as it would probably result in OOM, since many events would pile up */
    if (config_is_provisioning()) {
        DEBUG_EVENTS("skipping %s(%s) event (provisioning)", EVENT_TYPES_STR[type], port_id ? port_id : "null");
        return;
    }

    DEBUG_EVENTS("generating %s(%s) event", EVENT_TYPES_STR[type], port_id ? port_id : "null");

    sessions_push_event(type, port_id);

    if ((device_flags & DEVICE_FLAG_WEBHOOKS_ENABLED) && (BIT(type) & webhooks_events_mask)) {
        webhooks_push_event(type, port_id);
    }
}
