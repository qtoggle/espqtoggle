
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
#define API_MAX_DEVICE_DISP_NAME_LEN        64
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


ICACHE_FLASH_ATTR json_t                  * api_call_handle(int method, char* path, json_t *query_json,
                                                            json_t *request_json, int *code);

ICACHE_FLASH_ATTR void                      api_conn_set(struct espconn *conn, int access_level);
ICACHE_FLASH_ATTR bool                      api_conn_busy(void);
ICACHE_FLASH_ATTR bool                      api_conn_equal(struct espconn *conn);
ICACHE_FLASH_ATTR uint8                     api_conn_access_level_get(void);
ICACHE_FLASH_ATTR void                      api_conn_reset(void);

ICACHE_FLASH_ATTR json_t                  * port_to_json(port_t *port);
ICACHE_FLASH_ATTR json_t                  * device_to_json(void);


#endif /* _API_H */
