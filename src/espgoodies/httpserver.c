
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
#include <ctype.h>
#include <stdarg.h>
#include <mem.h>

#include "common.h"
#include "utils.h"
#include "httpserver.h"


#define RESPONSE_TEMPLATE_NO_BODY                           \
        "HTTP/1.1 %d %s\r\n"                                \
        "Server: %s\r\n"                                    \
        "Connection: close\r\n\r\n"

#define RESPONSE_TEMPLATE                                   \
        "HTTP/1.1 %d %s\r\n"                                \
        "Content-Type: %s\r\n"                              \
        "Cache-Control: no-cache\r\n"                       \
        "Server: %s\r\n"                                    \
        "Content-Length: %d\r\n"                            \
        "Connection: close\r\n"

#define STATUS_MSG_200              "OK"
#define STATUS_MSG_201              "Created"
#define STATUS_MSG_204              "No Content"

#define STATUS_MSG_400              "Bad Request"
#define STATUS_MSG_401              "Unauthorized"
#define STATUS_MSG_403              "Forbidden"
#define STATUS_MSG_404              "Not Found"

#define STATUS_MSG_500              "Internal Server Error"
#define STATUS_MSG_503              "Service Unavailable"


static char                       * server_name = NULL;


ICACHE_FLASH_ATTR static void       handle_header_value_ready(httpserver_context_t *hc);
ICACHE_FLASH_ATTR static void       handle_invalid(httpserver_context_t *hc, char c);
ICACHE_FLASH_ATTR static void       handle_request(httpserver_context_t *hc);


void handle_header_value_ready(httpserver_context_t *hc) {
    if (!strcasecmp(hc->header_name, "Content-Length")) {
        hc->content_length = strtol(hc->header_value, NULL, 10);
    }

    DEBUG_HTTPSERVER_CTX(hc, "header \"%s\" = \"%s\"", hc->header_name, hc->header_value);
    hc->header_names = realloc(hc->header_names, sizeof(char *) * (hc->header_count + 1));
    hc->header_values = realloc(hc->header_values, sizeof(char *) * (hc->header_count + 1));

    hc->header_names[hc->header_count] = strdup(hc->header_name);
    hc->header_values[hc->header_count++] = strdup(hc->header_value);
}

void handle_invalid(httpserver_context_t *hc, char c) {
    DEBUG_HTTPSERVER_CTX(hc, "unexpected character '%c' received in state %d", c, hc->req_state);

    if (hc->invalid_callback) {
        hc->invalid_callback(hc->callback_arg);
    }
}

void handle_request(httpserver_context_t *hc) {
    DEBUG_HTTPSERVER_CTX(hc, "body = \"%s\"", hc->body ? hc->body : "");
    DEBUG_HTTPSERVER_CTX(hc, "request ready");

    if (hc->request_callback) {
        hc->request_callback(hc->callback_arg, hc->method, hc->path, hc->query,
                             hc->header_names, hc->header_values, hc->header_count,
                             hc->body);
    }
}

void httpserver_set_name(char *name) {
    if (server_name) {
        free(server_name);
    }

    server_name = strdup(name);
    DEBUG_HTTPSERVER("server name set to %s", name);
}

void httpserver_register_callbacks(httpserver_context_t *hc, void *arg,
                                   http_invalid_callback_t ic,
                                   http_request_callback_t rc) {
    hc->callback_arg = arg;
    hc->invalid_callback = ic;
    hc->request_callback = rc;
}

void httpserver_parse_req_char(httpserver_context_t *hc, int c) {
    switch (hc->req_state) {
        case HTTP_STATE_IDLE:
        case HTTP_STATE_NEW: {
            if (isalpha(c)) {
                hc->req_state = HTTP_STATE_METHOD;
                append_max_len(hc->method_str, toupper(c), HTTP_MAX_METHOD_LEN);
            }
            else {
                hc->req_state = HTTP_STATE_INVALID;
                handle_invalid(hc, c);
            }
            
            break;
        }

        case HTTP_STATE_INVALID: {
            handle_invalid(hc, c);
            break;
        }

        case HTTP_STATE_METHOD: {
            if (isalpha(c)) {
                append_max_len(hc->method_str, toupper(c), HTTP_MAX_METHOD_LEN);
            }
            else if (isspace(c)) {
                hc->req_state = HTTP_STATE_METHOD_READY;
                if (!strcmp(hc->method_str, "GET")) {
                    hc->method = HTTP_METHOD_GET;
                }
                else if (!strcmp(hc->method_str, "HEAD")) {
                    hc->method = HTTP_METHOD_HEAD;
                }
                else if (!strcmp(hc->method_str, "OPTIONS")) {
                    hc->method = HTTP_METHOD_OPTIONS;
                }
                else if (!strcmp(hc->method_str, "POST")) {
                    hc->method = HTTP_METHOD_POST;
                }
                else if (!strcmp(hc->method_str, "PUT")) {
                    hc->method = HTTP_METHOD_PUT;
                }
                else if (!strcmp(hc->method_str, "PATCH")) {
                    hc->method = HTTP_METHOD_PATCH;
                }
                else if (!strcmp(hc->method_str, "DELETE")) {
                    hc->method = HTTP_METHOD_DELETE;
                }
                else {
                    hc->method = HTTP_METHOD_OTHER;
                }

                DEBUG_HTTPSERVER_CTX(hc, "method = \"%s\"", hc->method_str);
            }
            else {
                hc->req_state = HTTP_STATE_INVALID;
                handle_invalid(hc, c);
            }
            
            break;
        }

        case HTTP_STATE_METHOD_READY: {
            if (c == '/') { /* path starts */
                hc->req_state = HTTP_STATE_PATH;
                append_max_len(hc->path, c, HTTP_MAX_PATH_LEN);
            }
            
            break;
        }
        
        case HTTP_STATE_PATH: {
            if (c == '?') { /* query starts */
                hc->req_state = HTTP_STATE_QUERY;
            }
            else if (c == '#') { /* fragment starts */
                hc->req_state = HTTP_STATE_FRAGMENT;
            }
            else if (isspace(c)) { /* URI ends */
                hc->req_state = HTTP_STATE_FRAGMENT_READY;
            }
            else { /* still part of path */
                append_max_len(hc->path, c, HTTP_MAX_PATH_LEN);
                break;
            }

            DEBUG_HTTPSERVER_CTX(hc, "path = \"%s\"", hc->path);

            break;
        }

        case HTTP_STATE_QUERY: {
            if (c == '#') { /* fragment starts */
                hc->req_state = HTTP_STATE_FRAGMENT;
            }
            else if (isspace(c)) { /* URI ends */
                hc->req_state = HTTP_STATE_FRAGMENT_READY;
            }
            else { /* still part of query */
                append_max_len(hc->query, c, HTTP_MAX_QUERY_LEN);
                break;
            }

            DEBUG_HTTPSERVER_CTX(hc, "query = \"%s\"", hc->query);

            break;
        }

        case HTTP_STATE_FRAGMENT: {
            /* fragment is ignored */
            if (isspace(c)) { /* URI ends */
                hc->req_state = HTTP_STATE_FRAGMENT_READY;
            }

            break;
        }

        case HTTP_STATE_FRAGMENT_READY: {
            if (!isspace(c)) {
                hc->req_state = HTTP_STATE_PROTO;
            }

            break;
        }

        case HTTP_STATE_PROTO: {
            /* proto is ignored */
            if (isspace(c)) {
                hc->req_state = HTTP_STATE_PROTO_READY;
            }

            break;
        }

        case HTTP_STATE_PROTO_READY: {
            if (!isspace(c)) {
                hc->req_state = HTTP_STATE_HEADER_NAME;
                append_max_len(hc->header_name, c, HTTP_MAX_HEADER_NAME_LEN);
            }

            break;
        }

        case HTTP_STATE_HEADER_NAME: {
            if (c == ':' || isspace(c)) { /* header name value separator */
                hc->req_state = HTTP_STATE_HEADER_NAME_READY;
            }
            else { /* still part of header name */
                append_max_len(hc->header_name, c, HTTP_MAX_HEADER_NAME_LEN);
            }

            break;
        }

        case HTTP_STATE_HEADER_NAME_READY: {
            if (!isspace(c)) { /* header value starts */
                hc->req_state = HTTP_STATE_HEADER_VALUE;
                append_max_len(hc->header_value, c, HTTP_MAX_HEADER_VALUE_LEN);
            }

            break;
        }

        case HTTP_STATE_HEADER_VALUE: {
            if (c == '\r' || c == '\n') {
                handle_header_value_ready(hc);

                if (c == '\n') {
                    hc->req_state = HTTP_STATE_HEADER_VALUE_READY_NL;
                }
                else {
                    hc->req_state = HTTP_STATE_HEADER_VALUE_READY;
                }
                hc->header_name[0] = 0;
                hc->header_value[0] = 0;
            }
            else { /* still part of header value */
                append_max_len(hc->header_value, c, HTTP_MAX_HEADER_VALUE_LEN);
            }

            break;
        }

        case HTTP_STATE_HEADER_VALUE_READY: {
            if (isspace(c)) {
                if (c == '\n') { /* header line ready */
                    hc->req_state = HTTP_STATE_HEADER_VALUE_READY_NL;
                }
            }
            else { /* header name starts */
                hc->req_state = HTTP_STATE_HEADER_NAME;
                append_max_len(hc->header_name, c, HTTP_MAX_HEADER_NAME_LEN);
            }

            break;
        }

        case HTTP_STATE_HEADER_VALUE_READY_NL: {
            if (isspace(c)) {
                if (c == '\n') { /* header ready after second newline */
                    if (!hc->content_length) {
                        hc->req_state = HTTP_STATE_HEADER_READY;
                        handle_request(hc);
                    }
                    else if (hc->content_length == 1) {
                        hc->req_state = HTTP_STATE_BODY;
                    }
                    else {
                        hc->req_state = HTTP_STATE_HEADER_READY;
                    }
                }
            }
            else { /* header name starts */
                hc->req_state = HTTP_STATE_HEADER_NAME;
                append_max_len(hc->header_name, c, HTTP_MAX_HEADER_NAME_LEN);
            }

            break;
        }

        case HTTP_STATE_HEADER_READY: {
            if (hc->content_length) {
                hc->req_state = HTTP_STATE_BODY;
                hc->body_alloc_len = realloc_chunks(&(hc->body), 0, 1);
                hc->body_len = 1;
                hc->body[0] = c;
            }
            else { /* body unexpected */
                hc->req_state = HTTP_STATE_INVALID;
                handle_invalid(hc, c);
            }

            break;
        }
        
        case HTTP_STATE_BODY: {
            if (hc->body_len >= HTTP_MAX_BODY_LEN) {
                hc->req_state = HTTP_STATE_INVALID;
                handle_invalid(hc, c);
                break;
            }

            hc->body_alloc_len = realloc_chunks(&(hc->body), hc->body_alloc_len, hc->body_len + 1);
            hc->body[hc->body_len++] = c;

            if (hc->body_len >= hc->content_length) {
                hc->req_state = HTTP_STATE_BODY_READY;
                hc->body_alloc_len = realloc_chunks(&(hc->body), hc->body_alloc_len, hc->body_len + 1);
                hc->body[hc->body_len++] = 0;
                handle_request(hc);
            }

            break;
        }

        case HTTP_STATE_BODY_READY: {
            DEBUG_HTTPSERVER_CTX(hc, "ignoring extra body data");
            break;
        }
    }
}

void httpserver_reset_state(httpserver_context_t *hc) {
    if (hc->body) {
        free(hc->body);
        hc->body = NULL;
    }
    
    if (hc->header_count) {
        int i;
        for (i = 0; i < hc->header_count; i++) {
            free(hc->header_names[i]);
            free(hc->header_values[i]);
        }

        free(hc->header_names);
        free(hc->header_values);

        hc->header_count = 0;
    }

    int slot_index = hc->slot_index;
    memset(hc, 0, sizeof(httpserver_context_t));
    hc->slot_index = slot_index; /* restore slot index */
}

uint8 *httpserver_build_response(int status, char *content_type, char *header_names[], char *header_values[],
                                 int header_count, uint8 *body, int *len) {

    char *status_msg;
    uint8 *response = NULL;
    char h[256];
    int i, hl, response_len = 0;
    
    switch (status) {
        case 200:
            status_msg = STATUS_MSG_200;
            break;

        case 201:
            status_msg = STATUS_MSG_201;
            break;

        case 204:
            status_msg = STATUS_MSG_204;
            break;

        case 400:
            status_msg = STATUS_MSG_400;
            break;

        case 401:
            status_msg = STATUS_MSG_401;
            break;

        case 403:
            status_msg = STATUS_MSG_403;
            break;

        case 404:
            status_msg = STATUS_MSG_404;
            break;
            
        case 500:
            status_msg = STATUS_MSG_500;
            break;
            
        case 503:
            status_msg = STATUS_MSG_503;
            break;
            
        default:
            status_msg = STATUS_MSG_500;
    }
    
    /* start with response template */
    if (body && *len) {
        snprintf(h, 256, RESPONSE_TEMPLATE, status, status_msg, content_type, server_name, *len);
    }
    else {
        snprintf(h, 256, RESPONSE_TEMPLATE_NO_BODY, status, status_msg, server_name);
    }
    h[255] = 0;

    response_len = strlen(h);
    response = malloc(response_len + 1);
    strcpy((char *) response, h);

    /* add supplied headers */
    for (i = 0; i < header_count; i++) {
        hl = snprintf(h, 256, "%s: %s\r\n", header_names[i], header_values[i]);
        h[sizeof(h) - 1] = 0;
        response = realloc(response, response_len + hl + 1);
        strcpy((char *) response + response_len, h);
        response_len += hl;
    }

    if (body && *len) {
        int head_len = response_len;
        response_len += 2 /* \r\n head - body separator */;
        response_len += *len;
        response = realloc(response, response_len + 1);
        uint8 *head_end = response + head_len;
        strcpy((char *) head_end, "\r\n");
        memcpy(head_end + 2, body, *len);
    }
    
    *len = response_len;

#if defined(_DEBUG) && defined(_DEBUG_HTTPSERVER)
    char *s = malloc(*len + 1);
    strncpy(s, (char *) response, *len);
    s[*len] = 0;
    DEBUG_HTTPSERVER("response (%d bytes):\n----------------\n%s\n----------------", *len, s);
    free(s);
#endif

    return response;
}

#if defined(_DEBUG) && defined(_DEBUG_HTTPSERVER)

void DEBUG_HTTPSERVER_CTX(httpserver_context_t *hc, char *fmt, ...) {
    char new_fmt[128];
    char buf[1024];

    va_list args;
    va_start(args, fmt);
    
    snprintf(new_fmt, sizeof(new_fmt), "[httpserver    ] [%d.%d.%d.%d:%d/%d] %s",
             hc->ip[0], hc->ip[1], hc->ip[2], hc->ip[3], hc->port, hc->slot_index, fmt);

    vsnprintf(buf, sizeof(buf), new_fmt, args);
    DEBUG("%s", buf);
    va_end(args);
}

#endif /* _DEBUG && _DEBUG_HTTPSERVER */
