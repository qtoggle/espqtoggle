
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

#ifndef _API_H
#define _API_H


#include <ip_addr.h>
#include <espconn.h>

#include "espgoodies/json.h"

#include "ports.h"


#ifdef _DEBUG_API
#define DEBUG_API(fmt, ...)                 DEBUG("[api           ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_API(...)                      {}
#endif

#define API_MAX_FIELD_NAME_LEN              32
#define API_MAX_FIELD_VALUE_LEN             1024
#define API_MAX_FIELD_VALUE_LIST_LEN        4096

#define API_MAX_EXPR_LEN                    1024

#define API_MAX_DEVICE_NAME_LEN             32
#define API_MAX_DEVICE_DESC_LEN             64
#define API_MAX_DEVICE_CONFIG_MODEL_LEN     64

#define API_MIN_SEQUENCE_DELAY              1
#define API_MAX_SEQUENCE_DELAY              60000

#define API_MIN_SEQUENCE_REPEAT             0
#define API_MAX_SEQUENCE_REPEAT             65535

#define API_DEFAULT_LISTEN_TIMEOUT          60
#define API_MIN_LISTEN_TIMEOUT              1
#define API_MAX_LISTEN_TIMEOUT              3600
#define API_MAX_LISTEN_SESSION_ID_LEN       32

#define API_PORT_TYPE_BOOLEAN               "boolean"
#define API_PORT_TYPE_NUMBER                "number"

#define API_ATTR_TYPE_BOOLEAN               "boolean"
#define API_ATTR_TYPE_NUMBER                "number"
#define API_ATTR_TYPE_STRING                "string"

#define API_ACCESS_LEVEL_ADMIN              30
#define API_ACCESS_LEVEL_NORMAL             20
#define API_ACCESS_LEVEL_VIEWONLY           10
#define API_ACCESS_LEVEL_NONE               0

#define EMPTY_SHA256_HEX                    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"


ICACHE_FLASH_ATTR json_t          * api_call_handle(int method, char* path, json_t *query_json, json_t *request_json,
                                                    int *code);

ICACHE_FLASH_ATTR json_t          * port_to_json(port_t *port);
ICACHE_FLASH_ATTR json_t          * device_to_json(void);

ICACHE_FLASH_ATTR void              api_conn_set(struct espconn *conn, int access_level);
ICACHE_FLASH_ATTR bool              api_conn_busy(void);
ICACHE_FLASH_ATTR bool              api_conn_equal(struct espconn *conn);
ICACHE_FLASH_ATTR uint8             api_conn_access_level_get(void);
ICACHE_FLASH_ATTR void              api_conn_reset(void);


#endif /* _API_H */
