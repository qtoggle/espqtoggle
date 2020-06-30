
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

#ifndef _ESPGOODIES_HTTPSERVER_H
#define _ESPGOODIES_HTTPSERVER_H


#define HTTP_METHOD_GET                  1
#define HTTP_METHOD_HEAD                 2
#define HTTP_METHOD_OPTIONS              3
#define HTTP_METHOD_POST                 4
#define HTTP_METHOD_PUT                  5
#define HTTP_METHOD_PATCH                6
#define HTTP_METHOD_DELETE               7
#define HTTP_METHOD_OTHER                10

#define HTTP_STATE_IDLE                  0
#define HTTP_STATE_NEW                   1
#define HTTP_STATE_INVALID               2
#define HTTP_STATE_METHOD                3
#define HTTP_STATE_METHOD_READY          4
#define HTTP_STATE_PATH                  5
#define HTTP_STATE_QUERY                 6
#define HTTP_STATE_FRAGMENT              7
#define HTTP_STATE_FRAGMENT_READY        8
#define HTTP_STATE_PROTO                 9
#define HTTP_STATE_PROTO_READY           10
#define HTTP_STATE_HEADER_NAME           11
#define HTTP_STATE_HEADER_NAME_READY     12
#define HTTP_STATE_HEADER_VALUE          13
#define HTTP_STATE_HEADER_VALUE_READY    14
#define HTTP_STATE_HEADER_VALUE_READY_NL 15
#define HTTP_STATE_HEADER_READY          16
#define HTTP_STATE_BODY                  17
#define HTTP_STATE_BODY_READY            18

#define HTTP_MAX_METHOD_LEN              8
#define HTTP_MAX_PATH_LEN                64
#define HTTP_MAX_QUERY_LEN               64
#define HTTP_MAX_HEADER_NAME_LEN         32
#define HTTP_MAX_HEADER_VALUE_LEN        256
#define HTTP_MAX_BODY_LEN                10240


typedef void (*http_invalid_callback_t)(void *arg);
typedef void (*http_timeout_callback_t)(void *arg);
typedef void (*http_request_callback_t)(
    void *arg,
    int method,
    char *path,
    char *query,
    char *header_names[],
    char *header_values[],
    int header_count,
    char *body
);

typedef struct {

    uint8                   req_state;

    char                    method_str[HTTP_MAX_METHOD_LEN + 1];
    char                    path[HTTP_MAX_PATH_LEN + 1];
    char                    query[HTTP_MAX_QUERY_LEN + 1];

    uint8                   method;
    int                     content_length;

    char                  **header_names;
    char                  **header_values;
    uint8                   header_count;
    char                    header_name[HTTP_MAX_HEADER_NAME_LEN + 1];
    char                    header_value[HTTP_MAX_HEADER_VALUE_LEN + 1];

    char                   *body;
    int                     body_len;
    int                     body_alloc_len;

    http_invalid_callback_t invalid_callback;
    http_timeout_callback_t timeout_callback;
    http_request_callback_t request_callback;
    void                   *callback_arg;

    os_timer_t              timer;

    /* For debugging purposes */
    uint8                   slot_index;
    uint8                   ip[4];
    uint16                  port;

} httpserver_context_t;


#if defined(_DEBUG) && defined(_DEBUG_HTTPSERVER)
#define DEBUG_HTTPSERVER(fmt, ...) DEBUG("[httpserver    ] " fmt, ##__VA_ARGS__)
ICACHE_FLASH_ATTR void             DEBUG_HTTPSERVER_CTX(httpserver_context_t *hc, char *fmt, ...);
#else
#define DEBUG_HTTPSERVER(...)      {}
#define DEBUG_HTTPSERVER_CTX(...)  {}
#endif


ICACHE_FLASH_ATTR void   httpserver_set_name(char *name);
ICACHE_FLASH_ATTR void   httpserver_set_request_timeout(uint32 timeout);
ICACHE_FLASH_ATTR void   httpserver_setup_connection(
                             httpserver_context_t *hc,
                             void *arg,
                             http_invalid_callback_t ic,
                             http_timeout_callback_t tc,
                             http_request_callback_t rc
                         );
ICACHE_FLASH_ATTR void   httpserver_parse_req_char(httpserver_context_t *hc, int c);
ICACHE_FLASH_ATTR void   httpserver_context_reset(httpserver_context_t *hc);

    /* The response returned by this function must be freed after use */
ICACHE_FLASH_ATTR uint8 *httpserver_build_response(
                             int status,
                             char *content_type,
                             char *header_names[],
                             char *header_values[],
                             int header_count,
                             uint8 *body,
                             int *len
                         );


#endif /* _ESPGOODIES_HTTPSERVER_H */
