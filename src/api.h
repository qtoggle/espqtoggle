
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

#include "jsonrefs.h"
#include "peripherals.h"
#include "ports.h"


#ifdef _DEBUG_API
#define DEBUG_API(fmt, ...) DEBUG("[api           ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_API(...)      {}
#endif

#define API_MAX_FIELD_NAME_LEN         32
#define API_MAX_FIELD_VALUE_LEN        1024
#define API_MAX_FIELD_VALUE_LIST_LEN   4096

#define API_MAX_EXPR_LEN               1024

#define API_MAX_DEVICE_NAME_LEN        32
#define API_MAX_DEVICE_DISP_NAME_LEN   64
#define API_MAX_DEVICE_CONFIG_NAME_LEN 64

#define API_MIN_SEQUENCE_DELAY         1
#define API_MAX_SEQUENCE_DELAY         60000

#define API_MIN_SEQUENCE_REPEAT        0
#define API_MAX_SEQUENCE_REPEAT        65535

#define API_DEFAULT_LISTEN_TIMEOUT     60
#define API_MIN_LISTEN_TIMEOUT         1
#define API_MAX_LISTEN_TIMEOUT         3600
#define API_MAX_LISTEN_SESSION_ID_LEN  32

#define API_PORT_TYPE_BOOLEAN          "boolean"
#define API_PORT_TYPE_NUMBER           "number"

#define API_ATTR_TYPE_BOOLEAN          "boolean"
#define API_ATTR_TYPE_NUMBER           "number"
#define API_ATTR_TYPE_STRING           "string"

#define API_ACCESS_LEVEL_ADMIN         30
#define API_ACCESS_LEVEL_NORMAL        20
#define API_ACCESS_LEVEL_VIEWONLY      10
#define API_ACCESS_LEVEL_NONE          0


json_t ICACHE_FLASH_ATTR *api_call_handle(int method, char* path, json_t *query_json, json_t *request_json, int *code);

void   ICACHE_FLASH_ATTR  api_conn_set(struct espconn *conn, int access_level);
bool   ICACHE_FLASH_ATTR  api_conn_busy(void);
bool   ICACHE_FLASH_ATTR  api_conn_equal(struct espconn *conn);
uint8  ICACHE_FLASH_ATTR  api_conn_access_level_get(void);
void   ICACHE_FLASH_ATTR  api_conn_reset(void);
void   ICACHE_FLASH_ATTR  api_conn_save(void);
void   ICACHE_FLASH_ATTR  api_conn_restore(void);

json_t ICACHE_FLASH_ATTR *api_get_device(json_t *query_json, int *code);
json_t ICACHE_FLASH_ATTR *api_patch_device(json_t *query_json, json_t *request_json, int *code);

json_t ICACHE_FLASH_ATTR *api_post_reset(json_t *query_json, json_t *request_json, int *code);
#ifdef _OTA
json_t ICACHE_FLASH_ATTR *api_get_firmware(json_t *query_json, int *code);
json_t ICACHE_FLASH_ATTR *api_patch_firmware(json_t *query_json, json_t *request_json, int *code);
#endif
json_t ICACHE_FLASH_ATTR *api_get_access(json_t *query_json, int *code);
json_t ICACHE_FLASH_ATTR *api_get_ports(json_t *query_json, int *code);
json_t ICACHE_FLASH_ATTR *api_post_ports(json_t *query_json, json_t *request_json, int *code);
json_t ICACHE_FLASH_ATTR *api_patch_port(port_t *port, json_t *query_json, json_t *request_json, int *code);
json_t ICACHE_FLASH_ATTR *api_delete_port(port_t *port, json_t *query_json, int *code);

json_t ICACHE_FLASH_ATTR *api_get_port_value(port_t *port, json_t *query_json, int *code);
json_t ICACHE_FLASH_ATTR *api_patch_port_value(port_t *port, json_t *query_json, json_t *request_json, int *code);
json_t ICACHE_FLASH_ATTR *api_patch_port_sequence(port_t *port, json_t *query_json, json_t *request_json, int *code);

json_t ICACHE_FLASH_ATTR *api_get_webhooks(json_t *query_json, int *code);
json_t ICACHE_FLASH_ATTR *api_patch_webhooks(json_t *query_json, json_t *request_json, int *code);

json_t ICACHE_FLASH_ATTR *api_get_wifi(json_t *query_json, int *code);

json_t ICACHE_FLASH_ATTR *api_get_raw_io(char *io, json_t *query_json, int *code);
json_t ICACHE_FLASH_ATTR *api_patch_raw_io(char *io, json_t *query_json, json_t *request_json, int *code);

json_t ICACHE_FLASH_ATTR *api_get_peripherals(json_t *query_json, int *code);
json_t ICACHE_FLASH_ATTR *api_put_peripherals(json_t *query_json, json_t *request_json, int *code);
json_t ICACHE_FLASH_ATTR *api_get_system(json_t *query_json, int *code);
json_t ICACHE_FLASH_ATTR *api_put_system(json_t *query_json, json_t *request_json, int *code);

json_t ICACHE_FLASH_ATTR *port_to_json(port_t *port, json_refs_ctx_t *json_refs_ctx);
json_t ICACHE_FLASH_ATTR *device_to_json(void);
json_t ICACHE_FLASH_ATTR *peripheral_to_json(peripheral_t *peripheral);


#endif /* _API_H */
