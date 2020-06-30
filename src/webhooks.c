
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
#include <user_interface.h>

#include "espgoodies/common.h"
#include "espgoodies/crypto.h"
#include "espgoodies/httpclient.h"
#include "espgoodies/httputils.h"
#include "espgoodies/json.h"
#include "espgoodies/jwt.h"
#include "espgoodies/wifi.h"

#ifdef _OTA
#include "espgoodies/ota.h"
#endif

#include "common.h"
#include "device.h"
#include "events.h"
#include "jsonrefs.h"
#include "webhooks.h"


#define CONTENT_TYPE_HEADER     "Content-Type: application/json; charset=utf-8\r\n"
#define CONTENT_TYPE_HEADER_LEN 47


typedef struct webhooks_queue_node {

    event_t                    *event;
    char                        retries_left;
    struct webhooks_queue_node *next;

} webhooks_queue_node_t;


char   *webhooks_host = NULL;
uint16  webhooks_port = 80;
char   *webhooks_path = NULL;
char    webhooks_password_hash[SHA256_HEX_LEN + 1] = {0};
uint8   webhooks_events_mask = 0;
int     webhooks_timeout = 0;
int     webhooks_retries = 3;

static webhooks_queue_node_t *queue = NULL;
static int                    queue_len = 0;
static os_timer_t             later_timer;


ICACHE_FLASH_ATTR static void   process_queue(void);
ICACHE_FLASH_ATTR static void   process_queue_later(void);
ICACHE_FLASH_ATTR static void   on_process_queue_later(void *arg);
ICACHE_FLASH_ATTR static void   do_webhook_request(event_t *event);
ICACHE_FLASH_ATTR static void   on_webhook_response(
                                    char *body,
                                    int body_len,
                                    int status,
                                    char *header_names[],
                                    char *header_values[],
                                    int header_count,
                                    uint8 addr[]
                                );


void webhooks_push_event(int type, char *port_id) {
    event_t *event = malloc(sizeof(event_t));
    event->type = type;
    event->port_id = port_id ? strdup(port_id) : NULL;

    webhooks_queue_node_t *n, *pn;

    while (queue_len >= WEBHOOKS_MAX_QUEUE_LEN) {
        DEBUG_WEBHOOKS("dropping oldest event from queue");

        n = pn = queue;

        /* Find the oldest (first pushed, last in queue) event */
        while (n && n->next) {
            pn = n;
            n = n->next;
        }

        /* Drop the queued event */
        event_free(n->event);
        free(n);
        pn->next = NULL;

        queue_len--;
    }

    n = malloc(sizeof(webhooks_queue_node_t));
    n->event = event;
    n->next = queue;
    n->retries_left = webhooks_retries;
    queue = n;
    queue_len++;

    DEBUG_WEBHOOKS("pushing event of type \"%s\" (queue size=%d)", EVENT_TYPES_STR[type], queue_len);

    /* If this is the only item in queue, we need to start the processing */
    if (queue_len == 1) {
        process_queue();
    }
}


void process_queue() {
    if (!queue_len) {
        return;
    }

    /* Don't process webhooks queue while performing OTA */
#ifdef _OTA
    if (ota_busy()) {
        return;
    }
#endif

    if (!wifi_station_is_connected()) {
        DEBUG_WEBHOOKS("not connected, retrying in 1 second");

        process_queue_later();
        return;
    }

    webhooks_queue_node_t *n = queue;

    /* Find the oldest (first pushed, last in queue) event */
    while (n && n->next) {
        n = n->next;
    }

    if (n) {
        do_webhook_request(n->event);
    }
}

void process_queue_later() {
    os_timer_disarm(&later_timer);
    os_timer_setfn(&later_timer, on_process_queue_later, NULL);
    os_timer_arm(&later_timer, 1000, /* repeat = */ FALSE);
}

void on_process_queue_later(void *arg) {
    process_queue();
}

void do_webhook_request(event_t *event) {
    /* Make sure we have all the required parameters */
    if (!webhooks_host || !webhooks_path) {
        DEBUG_WEBHOOKS("some parameters are not configured");

        /* Manually call the callback */
        on_webhook_response(/* body = */ NULL, /* body_len = */ 0, /* status = */ 200,
                            /* header_names = */ NULL, /* header_values = */ NULL, /* header_count = */ 0,
                            /* addr = */ NULL);

        return;
    }

    /* URL */
    int url_len = 8; /* https:// */
    url_len += strlen(webhooks_host); /* www.example.com */
    url_len += 6; /* :65535 */
    url_len += strlen(webhooks_path); /* /path/to/events */

    char url[url_len + 1];
    snprintf(url, url_len, "%s://%s:%d%s",
             (device_flags & DEVICE_FLAG_WEBHOOKS_HTTPS) ? "https" : "http",
             webhooks_host, webhooks_port, webhooks_path);

    /* Add authorization header */
    json_t *claims = json_obj_new();
    json_obj_append(claims, "iss", json_str_new("qToggle"));
    json_obj_append(claims, "ori", json_str_new("device"));

    jwt_t *jwt = jwt_new(JWT_ALG_HS256, claims);
    json_free(claims);
    char *jwt_str = jwt_dump(jwt, webhooks_password_hash);
    jwt_free(jwt);

    char *auth_header = http_build_auth_token_header(jwt_str);
    free(jwt_str);

    int header_count = 1;
    char *header_names[1] = {"Authorization"};
    char *header_values[1] = {auth_header};

    json_refs_ctx_t json_refs_ctx;
    json_refs_ctx_init(&json_refs_ctx, JSON_REFS_TYPE_WEBHOOKS_EVENT);

    /* Body */
    json_t *event_json = event_to_json(event, &json_refs_ctx);
    if (event_json) {
        char *body = json_dump_r(event_json, /* free_mode = */ JSON_FREE_EVERYTHING);

        DEBUG_WEBHOOKS("request POST %s: %s", url, body);

        /* Actual request */
        httpclient_request("POST", url, (uint8 *) body, strlen(body), header_names, header_values, header_count,
                           on_webhook_response, webhooks_timeout);
    }

    free(auth_header);
}

void on_webhook_response(char *body, int body_len, int status, char *header_names[], char *header_values[],
                         int header_count, uint8 addr[]) {

    DEBUG_WEBHOOKS("response received: %d", status);

    webhooks_queue_node_t *n = queue, *pn = NULL;

    /* Find the oldest (first pushed, last in queue) event */
    while (n && n->next) {
        pn = n;
        n = n->next;
    }

    if (status == 200 || !n->retries_left) {
        if (!n->retries_left) {
            DEBUG_WEBHOOKS("no more retries left");
        }

        /* Remove event from queue */
        DEBUG_WEBHOOKS("removing event from queue");

        /* Free & remove the event */
        event_free(n->event);
        free(n);

        if (pn) {
            pn->next = NULL;
        }
        else {  /* Last event in queue */
            queue = NULL;
        }

        queue_len--;
        DEBUG_WEBHOOKS("queue size=%d", queue_len);
        if (queue_len) {
            process_queue();
        }
    }
    else {  /* Unsuccessful event, but retries left */
        DEBUG_WEBHOOKS("%d retries left, retrying (queue size=%d)", n->retries_left--, queue_len);
        process_queue_later();
    }
}
