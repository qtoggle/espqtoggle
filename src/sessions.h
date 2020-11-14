
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

#ifndef _SESSION_H
#define _SESSION_H


#include "espgoodies/json.h"

#include "api.h"
#include "events.h"
#include "ports.h"


#define SESSION_MAX_QUEUE_LEN 16
#define SESSION_COUNT         3

#ifdef _DEBUG_SESSIONS
#define DEBUG_SESSION(s, fmt, ...)          DEBUG("[sessions      ] [%s] " fmt, s, ##__VA_ARGS__)
#define DEBUG_SESSIONS(fmt, ...)            DEBUG("[sessions      ] " fmt, ##__VA_ARGS__)
#define DEBUG_SESSIONS_CONN(conn, fmt, ...) DEBUG_CONN("sessions", conn, fmt, ##__VA_ARGS__)
#else
#define DEBUG_SESSION(...)                  {}
#define DEBUG_SESSIONS(...)                 {}
#define DEBUG_SESSIONS_CONN(...)            {}
#endif


typedef struct session_queue_node {

    event_t                   *event;
    struct session_queue_node *next;

} session_queue_node_t;

typedef struct {

    /* A session is unused if the length of its id is 0 */
    char                  id[API_MAX_SESSION_ID_LEN + 1];
    session_queue_node_t *queue;
    int32                 queue_len;
    int32                 timeout;
    int32                 access_level;

    /* A session has a listen request attached when conn is not NULL */
    struct espconn       *conn;
    os_timer_t            timer;

} session_t;


extern session_t sessions[];


session_t ICACHE_FLASH_ATTR *session_find_by_id(char *id);
session_t ICACHE_FLASH_ATTR *session_find_by_conn(struct espconn *conn);
session_t ICACHE_FLASH_ATTR *session_create(char *id, struct espconn *conn, int timeout, int access_level);
void      ICACHE_FLASH_ATTR  session_respond(session_t *session);
void      ICACHE_FLASH_ATTR  session_reset(session_t *session);

void      ICACHE_FLASH_ATTR  sessions_push_event(int type, char *port_id);
void      ICACHE_FLASH_ATTR  sessions_respond_all(void);


#endif /* _SESSION_H */
