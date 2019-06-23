
/*
 * Copyright (c) Calin Crisan
 * This file is part of espqToggle.
 *
 * espqToggle is free software: you can redistribute it and/or modify
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

#ifndef _SESSION_H
#define _SESSION_H


#include "espgoodies/json.h"

#include "ports.h"
#include "events.h"
#include "api.h"


#define SESSION_MAX_QUEUE_LEN                   16
#define SESSION_COUNT                           4

#ifdef _DEBUG_SESSIONS
#define DEBUG_SESSION(s, fmt, ...)              DEBUG("[sessions      ] [%s] " fmt, s, ##__VA_ARGS__)
#define DEBUG_SESSIONS(fmt, ...)                DEBUG("[sessions      ] " fmt, ##__VA_ARGS__)
#define DEBUG_SESSIONS_CONN(conn, fmt, ...)     DEBUG_CONN("sessions", conn, fmt, ##__VA_ARGS__)
#else
#define DEBUG_SESSION(...)                      {}
#define DEBUG_SESSIONS(...)                     {}
#define DEBUG_SESSIONS_CONN(...)                {}
#endif


typedef struct session_queue_node {

    event_t                       * event;
    struct session_queue_node     * next;

} session_queue_node_t;

typedef struct {

    /* a session is unused if the length of its id is 0 */
    char                            id[API_MAX_LISTEN_SESSION_ID_LEN + 1];
    session_queue_node_t          * queue;
    int                             queue_len;
    int                             timeout;
    int                             access_level;

    /* a session has a listen request attached when conn is not NULL */
    struct espconn                * conn;
    os_timer_t                      timer;

} session_t;


extern session_t                    sessions[];


ICACHE_FLASH_ATTR session_t       * session_find_by_id(char *id);
ICACHE_FLASH_ATTR session_t       * session_find_by_conn(struct espconn *conn);
ICACHE_FLASH_ATTR session_t       * session_create(char *id, struct espconn *conn, int timeout, int access_level);
ICACHE_FLASH_ATTR void              session_respond(session_t *session);
ICACHE_FLASH_ATTR void              session_reset(session_t *session);

ICACHE_FLASH_ATTR void              sessions_push_event(int type, json_t *params, port_t *port);
ICACHE_FLASH_ATTR void              sessions_respond_all();


#endif /* _SESSION_H */
