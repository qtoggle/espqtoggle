
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
