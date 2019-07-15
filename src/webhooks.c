
/*
 * Copyright (c) Calin Crisan
 * This file is part of espQToggle.
 *
 * espQToggle is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>. 
 */

#include <mem.h>
#include <user_interface.h>

#include "espgoodies/common.h"
#include "espgoodies/json.h"
#include "espgoodies/httpclient.h"
#include "espgoodies/httputils.h"
#include "espgoodies/crypto.h"
#include "espgoodies/jwt.h"

#ifdef _OTA
#include "espgoodies/ota.h"
#endif

#include "device.h"
#include "events.h"
#include "webhooks.h"


#define CONTENT_TYPE_HEADER         "Content-Type: application/json; charset=utf-8\r\n"
#define CONTENT_TYPE_HEADER_LEN     47


typedef struct webhooks_queue_node {

    event_t                       * event;
    char                            retries_left;
    struct webhooks_queue_node    * next;

} webhooks_queue_node_t;


char *                          webhooks_host = NULL;
uint16                          webhooks_port = 80;
char *                          webhooks_path = NULL;
char                            webhooks_password_hash[SHA256_HEX_LEN + 1] = {0};
uint8                           webhooks_events_mask = 0;
int                             webhooks_timeout = 0;
int                             webhooks_retries = 3;

static webhooks_queue_node_t  * queue = NULL;
static int                      queue_len = 0;
os_timer_t                      later_timer;


ICACHE_FLASH_ATTR static void   process_queue(void);
ICACHE_FLASH_ATTR static void   process_queue_later(void);
ICACHE_FLASH_ATTR static void   on_process_queue_later(void *arg);
ICACHE_FLASH_ATTR static void   do_webhook_request(event_t *event);
ICACHE_FLASH_ATTR static void   on_webhook_response(char *body, int body_len, int status, char *header_names[],
                                                    char *header_values[], int header_count, uint8 addr[]);


void webhooks_push_event(int type, json_t *params, port_t *port) {
    event_t *event = malloc(sizeof(event_t));
    event->type = type;
    event->params = json_dup(params);
    event->port = port;

    webhooks_queue_node_t *n, *pn;

    while (queue_len >= WEBHOOKS_MAX_QUEUE_LEN) {
        DEBUG_WEBHOOKS("dropping oldest event from queue");

        n = pn = queue;

        /* find the oldest (first pushed, last in queue) event */
        while (n && n->next) {
            pn = n;
            n = n->next;
        }

        /* drop the queued event */
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

#if defined(_DEBUG) && defined(_DEBUG_WEBHOOKS)
    char *sparams = json_dump(params, /* also_free = */ FALSE);
    DEBUG_WEBHOOKS("pushing event of type \"%s\": %s (queue size=%d)", EVENT_TYPES_STR[type], sparams, queue_len);
    free(sparams);
#endif

    /* if this is the only item in queue,
     * we need to start the processing */
    if (queue_len == 1) {
        process_queue();
    }
}


void process_queue() {
    if (!queue_len) {
        return;
    }

    /* don't process webhooks queue while performing OTA */
#ifdef _OTA
    if (ota_busy()) {
        return;
    }
#endif

    bool connected = wifi_station_get_connect_status() == STATION_GOT_IP;
    if (!connected) {
        DEBUG_WEBHOOKS("not connected, retrying in 1 second");

        process_queue_later();
        return;
    }

    webhooks_queue_node_t *n = queue;

    /* find the oldest (first pushed, last in queue) event */
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
    /* make sure we have all the required parameters */
    if (!webhooks_host || !webhooks_path) {
        DEBUG_WEBHOOKS("some parameters are not configured");

        /* manually call the callback */
        on_webhook_response(/* body = */ NULL, /* body_len = */ 0, /* status = */ 200,
                            /* header_names = */ NULL, /* header_values = */ NULL, /* header_count = */ 0,
                            /* addr = */ NULL);

        return;
    }

    /* url */
    int url_len = 8; /* https:// */
    url_len += strlen(webhooks_host); /* www.example.com */
    url_len += 6; /* :65535 */
    url_len += strlen(webhooks_path); /* /path/to/events */

    char url[url_len + 1];
    snprintf(url, url_len, "%s://%s:%d%s",
             (device_flags & DEVICE_FLAG_WEBHOOKS_HTTPS) ? "https" : "http",
             webhooks_host, webhooks_port, webhooks_path);

    /* add authorization header */
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

    /* body */
    json_t *event_json = event_to_json(event);
    char *body = json_dump(event_json, /* also_free = */ TRUE);

    DEBUG_WEBHOOKS("request POST %s: %s", url, body);
    /* actual request */

    httpclient_request("POST", url, (uint8 *) body, strlen(body), header_names, header_values, header_count,
                       on_webhook_response, webhooks_timeout);

    free(body);
    free(auth_header);
}

void on_webhook_response(char *body, int body_len, int status, char *header_names[], char *header_values[],
                         int header_count, uint8 addr[]) {

    DEBUG_WEBHOOKS("response received: %d", status);

    webhooks_queue_node_t *n = queue, *pn = NULL;

    /* find the oldest (first pushed, last in queue) event */
    while (n && n->next) {
        pn = n;
        n = n->next;
    }

    if (status == 200 || !n->retries_left) {
        if (!n->retries_left) {
            DEBUG_WEBHOOKS("no more retries left");
        }

        /* remove event from queue */
        DEBUG_WEBHOOKS("removing event from queue");

        /* free & remove the event */
        event_free(n->event);
        free(n);

        if (pn) {
            pn->next = NULL;
        }
        else {  /* last event in queue */
            queue = NULL;
        }

        queue_len--;
        DEBUG_WEBHOOKS("queue size=%d", queue_len);
        if (queue_len) {
            process_queue();
        }
    }
    else {  /* unsuccessful event, but retries left */
        DEBUG_WEBHOOKS("%d retries left, retrying (queue size=%d)", n->retries_left--, queue_len);
        process_queue_later();
    }
}
