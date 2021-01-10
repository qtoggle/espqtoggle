
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
#include <string.h>
#include <mem.h>
#include <user_interface.h>

#include "espgoodies/common.h"
#include "espgoodies/tcpserver.h"
#include "espgoodies/utils.h"

#include "api.h"
#include "client.h"
#include "core.h"
#include "events.h"
#include "sessions.h"


session_t sessions[SESSION_COUNT];


static void    ICACHE_FLASH_ATTR   session_push(session_t *session, int type, char *port_id);
static void    ICACHE_FLASH_ATTR   session_dispose(session_t *session);
static event_t ICACHE_FLASH_ATTR **pop_all_events(session_t *session);
static void    ICACHE_FLASH_ATTR   on_session_timeout(void *arg);


session_t *session_find_by_id(char *id) {
    int i;
    for (i = 0; i < SESSION_COUNT; i++) {
        if (!strcmp(sessions[i].id, id)) {
            DEBUG_SESSION(id, "found at slot %d", i);
            return sessions + i;
        }
    }

    DEBUG_SESSIONS("no \"%s\" found", id);

    return NULL;
}

session_t *session_find_by_conn(struct espconn *conn) {
    int i;
    for (i = 0; i < SESSION_COUNT; i++) {
        if (sessions[i].id[0] && sessions[i].conn && CONN_EQUAL(sessions[i].conn, conn)) {
            DEBUG_SESSIONS_CONN(conn, "found %s at slot %d", sessions[i].id, i);
            return sessions + i;
        }
    }

    return NULL;
}

session_t *session_create(char *id, struct espconn *conn, int timeout, int access_level) {
    int i, free_slot = -1;
    for (i = 0; i < SESSION_COUNT; i++) {
        if (!sessions[i].id[0]) {
            free_slot = i;
            DEBUG_SESSIONS("found unused session at slot %d", i);
            break;
        }
    }

    if  (free_slot == -1) {
        DEBUG_SESSIONS("all available listen sessions are in use, freeing up first slot");
        session_respond(sessions + 0);
        session_dispose(sessions + 0);
        free_slot = 0;
    }

    /* Initialize the session */
    session_t *session = sessions + free_slot;
    strncpy(session->id, id, API_MAX_SESSION_ID_LEN);
    session->id[API_MAX_SESSION_ID_LEN] = 0;
    session->queue = NULL;
    session->queue_len = 0;
    session->timeout = timeout;
    session->access_level = access_level;
    session->conn = conn;

    DEBUG_SESSIONS("assigned id \"%s\" to slot %d", id, free_slot);

    return session;
}

void session_respond(session_t *session) {
    if (!session->conn) {
        DEBUG_SESSION(session->id, "session has no connection");
        return;
    }

    DEBUG_SESSIONS_CONN(session->conn, "responding to %s with %d events", session->id, session->queue_len);

    json_t *response_json = json_list_new();
    json_t *event_json;

    json_refs_ctx_t json_refs_ctx;
    json_refs_ctx_init(&json_refs_ctx, JSON_REFS_TYPE_LISTEN_EVENTS_LIST);

    event_t *e, **event, **events = pop_all_events(session);
    event = events;
    while ((e = *event++)) {
        event_json = event_to_json(e, &json_refs_ctx);
        if (event_json) {
            json_list_append(response_json, event_json);
        }
        event_free(e);

        json_refs_ctx.index++;
    }

    free(events);

    respond_json(session->conn, 200, response_json);
    session->conn = NULL;
}

void session_reset(session_t *session) {
    os_timer_disarm(&session->timer);
    os_timer_setfn(&session->timer, on_session_timeout, session);
    os_timer_arm(&session->timer, session->timeout * 1000, /* repeat = */ FALSE);
}

void sessions_push_event(int type, char *port_id) {
    /* Push the event to each active session */
    int i;
    session_t *session;
    for (i = 0; i < SESSION_COUNT; i++) {
        session = sessions + i;
        if (!session->id[0]) { /* Not active */
            continue;
        }

        if (session->access_level < EVENT_ACCESS_LEVELS[type]) {
            continue; /* Not permitted for this access level */
        }

        session_push(session, type, port_id);
        if (session->conn) {
            core_listen_respond(session);
        }
    }
}

void sessions_respond_all(void) {
    /* Respond with "busy" to all active sessions */
    int i;
    session_t *session;
    for (i = 0; i < SESSION_COUNT; i++) {
        session = sessions + i;
        if (!session->id[0]) { /* Not active */
            continue;
        }

        DEBUG_SESSION(session->id, "responding with busy");

        if (session->conn) {
            DEBUG_SESSIONS_CONN(session->conn, "responding to %s with busy", session->id);

            json_t *response_json = json_obj_new();
            json_obj_append(response_json, "error", json_str_new("busy"));

            respond_json(session->conn, 503, response_json);
            session->conn = NULL;
        }

        session_reset(session);
    }
}

void session_push(session_t *session, int type, char *port_id) {
    event_t *event = event_new(type, port_id);

    session_queue_node_t *n, *pn;

    /* Deduplicate update events */
    if (type == EVENT_TYPE_PORT_UPDATE ||
        type == EVENT_TYPE_DEVICE_UPDATE ||
        type == EVENT_TYPE_FULL_UPDATE) {

        n = session->queue;
        pn = NULL;

        /* Find the oldest (first pushed, last in queue) event */
        while (n) {
            if ((n->event->type == type) && (!port_id || !strcmp(port_id, n->event->port_id))) {
                DEBUG_SESSION(session->id, "dropping similar %s event", EVENT_TYPES_STR[type]);

                /* Drop the event */
                if (pn) {
                    pn->next = n->next;
                }
                else {
                    session->queue = n->next;
                }
                event_free(n->event);
                free(n);
                session->queue_len--;
            }

            pn = n;
            n = n->next;
        }
    }

    /* Make room for new event, if needed */
    while (session->queue_len >= SESSION_MAX_QUEUE_LEN) {
        DEBUG_SESSION(session->id, "dropping oldest event");
    
        n = pn = session->queue;

        /* Find the oldest (first pushed, last in queue) event */
        while (n && n->next) {
            pn = n;
            n = n->next;
        }

        /* Drop the event */
        event_free(n->event);
        free(n);
        pn->next = NULL;

        session->queue_len--;
    }
    
    n = malloc(sizeof(session_queue_node_t));
    n->event = event;
    n->next = session->queue;
    session->queue = n;
    session->queue_len++;

    DEBUG_SESSION(
        session->id,
        "pushing event of type \"%s\" (queue length=%d)",
        EVENT_TYPES_STR[type],
        session->queue_len
    );
}

void session_dispose(session_t *session) {
    DEBUG_SESSION(session->id, "freeing");

    session->id[0] = 0;

    session_queue_node_t *n, *pn;
    n = session->queue;

    while (n) {
        event_free(n->event);
        pn = n;
        n = n->next;
        free(pn);
    }

    session->queue_len = 0;
    session->conn = NULL;
    session->timeout = 0;
    os_timer_disarm(&session->timer);
}

event_t **pop_all_events(session_t *session) {
    event_t **events = malloc(sizeof(event_t *) * (session->queue_len + 1));
    session_queue_node_t *n = session->queue, *pn;
    
    /* Place the events in reverse order into a list of pointers to events */
    int i = 0;
    while (n) {
        events[session->queue_len - i - 1] = n->event;
        pn = n;
        n = n->next;
        free(pn);
        i++;
    }

    /* Set the NULL terminator */
    events[session->queue_len] = NULL;
    
    session->queue = NULL;
    session->queue_len = 0;

    return events;
}

void on_session_timeout(void *arg) {
    session_t *session = arg;
    
    if (session->conn) {
        DEBUG_SESSIONS_CONN(session->conn, "listen keep-alive for session %s", session->id);
        session_respond(session);
        session_reset(session);
    }
    else {
        DEBUG_SESSION(session->id, "expired");
        session_dispose(session);
    }
}
