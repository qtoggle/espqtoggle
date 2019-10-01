
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

#ifndef _CLIENT_H
#define _CLIENT_H


#include "espgoodies/json.h"


#ifdef _DEBUG_ESPQTCLIENT
#define DEBUG_ESPQTCLIENT(fmt, ...)             DEBUG("[espqtclient   ] " fmt, ##__VA_ARGS__)
#define DEBUG_ESPQTCLIENT_CONN(conn, fmt, ...)  DEBUG_CONN("espqtclient", conn, fmt, ##__VA_ARGS__)
#else
#define DEBUG_ESPQTCLIENT(...)                  {}
#define DEBUG_ESPQTCLIENT_CONN(...)             {}
#endif



ICACHE_FLASH_ATTR void                          client_init(void);

    /* respond_json() will free the json structure by itself, using json_free() ! */
ICACHE_FLASH_ATTR void                          respond_json(struct espconn *conn, int status, json_t *json);
ICACHE_FLASH_ATTR void                          respond_error(struct espconn *conn, int status, char *error);

ICACHE_FLASH_ATTR void                          respond_html(struct espconn *conn, int status, uint8 *html, int len);


#endif /* _CLIENT_H */
