
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

#ifdef _OTA
#include "espgoodies/ota.h"
#endif

#include "device.h"
#include "sessions.h"
#include "webhooks.h"
#include "api.h"
#include "events.h"


char *EVENT_TYPES_STR[] = {NULL, "value-change", "port-update", "port-add", "port-remove", "device-update"};

int EVENT_ACCESS_LEVELS[] = {
    0, /* offset */
    API_ACCESS_LEVEL_VIEWONLY,    /* value-change */
    API_ACCESS_LEVEL_VIEWONLY,    /* port-update */
    API_ACCESS_LEVEL_VIEWONLY,    /* port-add */
    API_ACCESS_LEVEL_VIEWONLY,    /* port-remove */
    API_ACCESS_LEVEL_ADMIN        /* device-update */
};


void event_push(int type, json_t *params, port_t *port) {
    /* don't push any event while performing OTA */
#ifdef _OTA
    if (ota_busy()) {
        return;
    }
#endif

    sessions_push_event(type, params, port);

    if ((device_flags & DEVICE_FLAG_WEBHOOKS_ENABLED) && ((1 << type) & webhooks_events_mask)) {
        webhooks_push_event(type, params, port);
    }
}

void event_push_value_change(port_t *port) {
    json_t *params = json_obj_new();
    json_obj_append(params, "id", json_str_new(port->id));

    /* we'll add the actual value later as it may change again during this iteration */

    event_push(EVENT_TYPE_VALUE_CHANGE, params, port);
    json_free(params);
}

void event_push_port_update(port_t *port) {
    json_t *params = port_to_json(port);
    event_push(EVENT_TYPE_PORT_UPDATE, params, NULL);
    json_free(params);
}

void event_push_port_add(port_t *port) {
    json_t *params = port_to_json(port);
    event_push(EVENT_TYPE_PORT_ADD, params, NULL);
    json_free(params);
}

void event_push_port_remove(char *port_id) {
    json_t *params = json_obj_new();
    json_obj_append(params, "id", json_str_new(port_id));
    event_push(EVENT_TYPE_PORT_REMOVE, params, NULL);
    json_free(params);
}

void event_push_device_update(void) {
    json_t *params = device_to_json();
    event_push(EVENT_TYPE_DEVICE_UPDATE, params, NULL);
    json_free(params);
}

json_t *event_to_json(event_t *event) {
    json_t *json = json_obj_new();

    json_obj_append(json, "type", json_str_new(EVENT_TYPES_STR[event->type]));

    json_t *params = json_dup(event->params);

    if (event->type == EVENT_TYPE_VALUE_CHANGE) {
        /* the actual value is captured as late as possible */

        json_obj_append(params, "value", port_get_json_value(event->port));
    }

    json_obj_append(json, "params", params);

    return json;
}

void event_free(event_t *event) {
    if (event->params) {
        json_free(event->params);
    }

    free(event);
}
