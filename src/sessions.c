
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

#include <stdlib.h>
#include <string.h>
#include <mem.h>
#include <user_interface.h>

#include "espgoodies/common.h"
#include "espgoodies/tcpserver.h"
#include "espgoodies/utils.h"

#include "api.h"
#include "client.h"
#include "events.h"
#include "core.h"
#include "sessions.h"


session_t                               sessions[SESSION_COUNT];


ICACHE_FLASH_ATTR static void           session_push(session_t *session, int type, json_t *params, port_t *port);
ICACHE_FLASH_ATTR static void           session_free(session_t *session);
ICACHE_FLASH_ATTR static event_t **     pop_all_events(session_t *session);
ICACHE_FLASH_ATTR static void           on_session_timeout(void *arg);


session_t *session_find_by_id(char *id) {
    int i;
    for (i = 0; i < SESSION_COUNT; i++) {
        if (!strcmp(sessions[i].id, id)) {
            DEBUG_SESSION(id, "found at slot %d", i);
            return sessions + i;
        }
    }

    DEBUG_SESSIONS("no %s found", id);

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
    int i;
    for (i = 0; i < SESSION_COUNT; i++) {
        if (!sessions[i].id[0]) {
            /* initialize the session */
            strncpy(sessions[i].id, id, API_MAX_LISTEN_SESSION_ID_LEN);
            sessions[i].id[API_MAX_LISTEN_SESSION_ID_LEN] = 0;
            sessions[i].queue = NULL;
            sessions[i].queue_len = 0;
            sessions[i].timeout = timeout;
            sessions[i].access_level = access_level;
            sessions[i].conn = conn;

            DEBUG_SESSIONS("found unused session at slot %d and assigned id %s", i, id);

            return sessions + i;
        }
    }

    DEBUG_SESSIONS("all available listen sessions are in use");

    return NULL;
}

void session_respond(session_t *session) {
    if (!session->conn) {
        DEBUG_SESSION(session->id, "session has no connection");
        return;
    }

    DEBUG_SESSIONS_CONN(session->conn, "responding to %s with %d events", session->id, session->queue_len);

    json_t *response_json = json_list_new();

    event_t *e, **event, **events = pop_all_events(session);
    event = events;
    while ((e = *event++)) {
        json_list_append(response_json, event_to_json(e));
        event_free(e);
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

void sessions_push_event(int type, json_t *params, port_t *port) {
    /* push the event to each active session */
    int i;
    session_t *session;
    for (i = 0; i < SESSION_COUNT; i++) {
        session = sessions + i;
        if (!session->id[0]) { /* not active */
            continue;
        }

        if (session->access_level < EVENT_ACCESS_LEVELS[type]) {
            continue; /* not permitted for this access level */
        }

        session_push(session, type, json_dup(params), port);
        if (session->conn) {
            core_listen_respond(session);
        }
    }
}

void sessions_respond_all(void) {
    /* respond with "busy" to all active sessions */
    int i;
    session_t *session;
    for (i = 0; i < SESSION_COUNT; i++) {
        session = sessions + i;
        if (!session->id[0]) { /* not active */
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

void session_push(session_t *session, int type, json_t *params, port_t *port) {
    event_t *event = malloc(sizeof(event_t));
    event->type = type;
    event->params = params;
    event->port = port;
    
    session_queue_node_t *n, *pn;

    while (session->queue_len >= SESSION_MAX_QUEUE_LEN) {
        DEBUG_SESSION(session->id, "dropping oldest event");
    
        n = pn = session->queue;

        /* find the oldest (first pushed, last in queue) event */
        while (n && n->next) {
            pn = n;
            n = n->next;
        }

        /* drop the event */
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

#ifdef _DEBUG
    char *sparams = json_dump(params, /* also_free = */ FALSE);
    DEBUG_SESSION(session->id, "pushing event of type \"%s\": %s (size=%d)", EVENT_TYPES_STR[type], sparams,
                  session->queue_len);
    free(sparams);
#endif
}

void session_free(session_t *session) {
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
    
    /* place the events in reverse order into a list of pointers to events */
    int i = 0;
    while (n) {
        events[session->queue_len - i - 1] = n->event;
        pn = n;
        n = n->next;
        free(pn);
        i++;
    }

    /* set the NULL terminator */
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
        session_free(session);
    }
}
