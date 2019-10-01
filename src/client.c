
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

#include <stdlib.h>
#include <mem.h>
#include <c_types.h>
#include <user_interface.h>

#include "espgoodies/common.h"
#include "espgoodies/tcpserver.h"
#include "espgoodies/httpserver.h"
#include "espgoodies/httpclient.h"
#include "espgoodies/httputils.h"
#include "espgoodies/html.h"
#include "espgoodies/crypto.h"
#include "espgoodies/jwt.h"
#include "espgoodies/utils.h"
#include "espgoodies/system.h"

#ifdef _OTA
#include "espgoodies/ota.h"
#endif

#include "ver.h"
#include "device.h"
#include "sessions.h"
#include "api.h"
#include "client.h"


#define MAX_PARALLEL_HTTP_REQ       4
#define JSON_CONTENT_TYPE           "application/json; charset=utf-8"
#define HTML_CONTENT_TYPE           "text/html; charset=utf-8"

#define RESPOND_UNAUTHENTICATED()   respond_error(conn, 401, "authentication required");


static httpserver_context_t         http_state_array[MAX_PARALLEL_HTTP_REQ];

static char                       * unprotected_paths[] = {"/access", NULL};


ICACHE_FLASH_ATTR static void     * on_tcp_conn(struct espconn *conn);
ICACHE_FLASH_ATTR static void       on_tcp_recv(struct espconn *conn, httpserver_context_t *hc, char *data, int len);
ICACHE_FLASH_ATTR static void       on_tcp_sent(struct espconn *conn, httpserver_context_t *hc);
ICACHE_FLASH_ATTR static void       on_tcp_disc(struct espconn *conn, httpserver_context_t *hc);

ICACHE_FLASH_ATTR static void       on_invalid_http_request(struct espconn *conn);
ICACHE_FLASH_ATTR static void       on_http_request(struct espconn *conn, int method, char *path, char *query,
                                                    char *header_names[], char *header_values[], int header_count,
                                                    char *body);


void *on_tcp_conn(struct espconn *conn) {
    /* find first free http context slot */
    httpserver_context_t *hc = NULL;
    int i;

    for (i = 0; i < MAX_PARALLEL_HTTP_REQ; i++) {
        if (http_state_array[i].req_state == HTTP_STATE_IDLE) {
            hc = http_state_array + i;
            break;
        }
    }

    if (!hc) {
        DEBUG_ESPQTCLIENT_CONN(conn, "too many parallel HTTP requests");
        respond_error(conn, 503, "busy");
        return NULL;
    }

    hc->req_state = HTTP_STATE_NEW;

    /* for debugging purposes */
    memcpy(hc->ip, conn->proto.tcp->remote_ip, 4);
    hc->port = conn->proto.tcp->remote_port;

    httpserver_register_callbacks(hc, conn,
                                  (http_invalid_callback_t) on_invalid_http_request,
                                  (http_request_callback_t) on_http_request);

    return hc;
}

void on_tcp_recv(struct espconn *conn, httpserver_context_t *hc, char *data, int len) {
    if (!hc) {
        DEBUG_ESPQTCLIENT_CONN(conn, "ignoring data from rejected client");
        return;
    }

    int i;
    for (i = 0; i < len; i++) {
        httpserver_parse_req_char(hc, data[i]);
    }
}

void on_tcp_sent(struct espconn *conn, httpserver_context_t *hc) {
    tcp_disconnect(conn);
}

void on_tcp_disc(struct espconn *conn, httpserver_context_t *hc) {
    if (!hc) {
        return;
    }

    /* see if the connection corresponds to a listening session */
    session_t *session = session_find_by_conn(conn);
    if (session) { /* a listen connection */
        DEBUG_ESPQTCLIENT_CONN(conn, "listen client unexpectedly disconnected");
        session->conn = NULL;
        session_reset(session);
    }

    /* see if the connection is asynchronous and is currently waiting for a response */
    if (api_conn_equal(conn)) {
        DEBUG_ESPQTCLIENT_CONN(conn, "canceling async response to closed connection");
        api_conn_reset();
    }

    httpserver_reset_state(hc);
}

/* http request/response handling */

void on_invalid_http_request(struct espconn *conn) {
    DEBUG_ESPQTCLIENT_CONN(conn, "ignoring invalid request");

    respond_error(conn, 400, "malformed request");
}

void on_http_request(struct espconn *conn, int method, char *path, char *query,
                     char *header_names[], char *header_values[], int header_count,
                     char *body) {

    json_t *request_json = NULL;
    json_t *query_json = http_parse_url_encoded(query);
    json_t *response_json = NULL;
    char *jwt_str = NULL;
    jwt_t *jwt = NULL;
    int i;
    uint8 access_level = API_ACCESS_LEVEL_NONE;

    if (api_conn_busy()) {
        DEBUG_ESPQTCLIENT_CONN(conn, "api busy");
        respond_error(conn, 503, "busy");
        goto done;
    }

    DEBUG_ESPQTCLIENT_CONN(conn, "processing request %s %s",
                           method == HTTP_METHOD_GET ? "GET" :
                           method == HTTP_METHOD_POST ? "POST" :
                           method == HTTP_METHOD_PUT ? "PUT" :
                           method == HTTP_METHOD_PATCH ? "PATCH" :
                           method == HTTP_METHOD_DELETE ? "DELETE" : "OTHER", path);

    if (method == HTTP_METHOD_POST || method == HTTP_METHOD_PATCH || method == HTTP_METHOD_PUT) {
        if (body) {
            request_json = json_parse(body);
            if (!request_json) {
                /* invalid json */
                DEBUG_ESPQTCLIENT_CONN(conn, "invalid json");
                respond_error(conn, 400, "malformed body");
                goto done;
            }
        }
        else {
            request_json = json_obj_new();
        }
    }

    /* automatically grant admin access level in setup mode */
    if (system_setup_mode_active()) {
        access_level = API_ACCESS_LEVEL_ADMIN;
        goto skip_auth;
    }

    bool unprotected = FALSE;
    char *p, **paths = unprotected_paths;
    while ((p = *paths++)) {
        if (!strcmp(p, path)) {
            unprotected = TRUE;
            break;
        }
    }

    /* look for Authorization header */
    char *authorization = NULL;
    for (i = 0; i < header_count; i++) {
        if (!strcasecmp(header_names[i], "Authorization")) {
            authorization = header_values[i];
            break;
        }
    }

    if (!authorization) {
        if (unprotected) {
            goto skip_auth;
        }

        if (!strncmp(device_admin_password_hash, EMPTY_SHA256_HEX, SHA256_HEX_LEN)) {
            DEBUG_ESPQTCLIENT_CONN(conn, "granting admin rights due to empty admin password");
            access_level = API_ACCESS_LEVEL_ADMIN;
            goto skip_auth;
        }

        DEBUG_ESPQTCLIENT_CONN(conn, "missing authorization header");
        RESPOND_UNAUTHENTICATED();
        goto done;
    }

    /* parse Authorization header */
    jwt_str = http_parse_auth_token_header(authorization);
    if (!jwt_str) {
        if (unprotected) {
            goto skip_auth;
        }

        DEBUG_ESPQTCLIENT_CONN(conn, "invalid authorization header");
        RESPOND_UNAUTHENTICATED();
        goto done;
    }

    /* parse & validate JWT token */
    jwt = jwt_parse(jwt_str);
    if (!jwt) {
        if (unprotected) {
            goto skip_auth;
        }

        DEBUG_ESPQTCLIENT_CONN(conn, "invalid JWT");
        RESPOND_UNAUTHENTICATED();
        goto done;
    }

    if (jwt->alg != JWT_ALG_HS256) {
        if (unprotected) {
            goto skip_auth;
        }

        DEBUG_ESPQTCLIENT_CONN(conn, "unknown JWT algorithm");
        RESPOND_UNAUTHENTICATED();
        goto done;
    }

    /* retrieve & validate issuer claim */
    json_t *issuer_json = json_obj_lookup_key(jwt->claims, "iss");
    if (!issuer_json || json_get_type(issuer_json) != JSON_TYPE_STR || strcmp(json_str_get(issuer_json), "qToggle")) {
        if (unprotected) {
            goto skip_auth;
        }

        DEBUG_ESPQTCLIENT_CONN(conn, "missing or invalid iss claim in JWT");
        RESPOND_UNAUTHENTICATED();
        goto done;
    }

    /* retrieve & validate origin claim */
    json_t *origin_json = json_obj_lookup_key(jwt->claims, "ori");
    if (!origin_json || json_get_type(origin_json) != JSON_TYPE_STR || strcmp(json_str_get(origin_json), "consumer")) {
        if (unprotected) {
            goto skip_auth;
        }

        DEBUG_ESPQTCLIENT_CONN(conn, "missing or invalid ori claim in JWT");
        RESPOND_UNAUTHENTICATED();
        goto done;
    }

    // TODO validate iat claim - when we have real date/time support

    /* retrieve username claim */
    json_t *username_json = json_obj_lookup_key(jwt->claims, "usr");
    if (!username_json || json_get_type(username_json) != JSON_TYPE_STR) {
        if (unprotected) {
            goto skip_auth;
        }

        DEBUG_ESPQTCLIENT_CONN(conn, "missing or invalid usr claim in JWT");
        RESPOND_UNAUTHENTICATED();
        goto done;
    }

    /* validate username */
    bool valid = FALSE;
    char *username = json_str_get(username_json);
    if (!strcmp(username, "admin")) {
        valid = jwt_validate(jwt_str, jwt->alg, device_admin_password_hash);
        access_level = API_ACCESS_LEVEL_ADMIN;
        DEBUG_ESPQTCLIENT_CONN(conn, "admin user claimed");
    }
    else if (!strcmp(username, "normal")) {
        valid = jwt_validate(jwt_str, jwt->alg, device_normal_password_hash);
        access_level = API_ACCESS_LEVEL_NORMAL;
        DEBUG_ESPQTCLIENT_CONN(conn, "normal user claimed");
    }
    else if (!strcmp(username, "viewonly")) {
        valid = jwt_validate(jwt_str, jwt->alg, device_viewonly_password_hash);
        access_level = API_ACCESS_LEVEL_VIEWONLY;
        DEBUG_ESPQTCLIENT_CONN(conn, "view-only user claimed");
    }
    else {
        if (unprotected) {
            goto skip_auth;
        }

        DEBUG_ESPQTCLIENT_CONN(conn, "unknown usr in JWT");
        RESPOND_UNAUTHENTICATED();
        goto done;
    }

    if (!valid) {
        access_level = API_ACCESS_LEVEL_NONE;

        if (unprotected) {
            goto skip_auth;
        }

        DEBUG_ESPQTCLIENT_CONN(conn, "invalid JWT signature");
        RESPOND_UNAUTHENTICATED();
        goto done;
    }

    DEBUG_ESPQTCLIENT_CONN(conn, "JWT valid, access granted");

    free(jwt_str);
    jwt_str = NULL;

    skip_auth:

    /* treat the listen API call separately */
    if (!strncmp(path, "/listen", 7) && method == HTTP_METHOD_GET) {
        DEBUG_ESPQTCLIENT_CONN(conn, "received listen request");

        if (access_level < API_ACCESS_LEVEL_VIEWONLY) {
            json_t *json = json_obj_new();
            json_obj_append(json, "error", json_str_new("forbidden"));
            json_obj_append(json, "required_level", json_str_new("viewonly"));

            respond_json(conn, 403, json);
            goto done;
        }

#ifdef _OTA
        /* no listening while OTA active */
        if (ota_busy()) {
            DEBUG_ESPQTCLIENT_CONN(conn, "cannot accept listen requests while OTA active");
            respond_error(conn, 503, "busy");
            goto done;
        }
#endif

        /* session_id argument */
        json_t *session_id_json = json_obj_lookup_key(query_json, "session_id");
        if (!session_id_json) {
            respond_error(conn, 400, "missing field: session_id");
            goto done;
        }
        char *session_id = json_str_get(session_id_json);

        char c, *s = session_id;
        while ((c = *s++)) {
            if (!isalnum((int) c) && (c != '-')) {
                respond_error(conn, 400, "invalid field: session_id");
                goto done;
            }
        }

        if (strlen(session_id) > API_MAX_LISTEN_SESSION_ID_LEN) {
            respond_error(conn, 400, "invalid field: session_id");
            goto done;
        }

        /* timeout argument */
        json_t *timeout_json = json_obj_lookup_key(query_json, "timeout");
        int timeout = API_DEFAULT_LISTEN_TIMEOUT;
        if (timeout_json) {
            char *e;
            timeout = strtol(json_str_get(timeout_json), &e, 10);

            if (*e) {  /* invalid integer */
                respond_error(conn, 400, "invalid field: timeout");
                goto done;
            }

            if (timeout < API_MIN_LISTEN_TIMEOUT || timeout > API_MAX_LISTEN_TIMEOUT) {
                respond_error(conn, 400, "invalid field: timeout");
                goto done;
            }
        }

        DEBUG_ESPQTCLIENT_CONN(conn, "listen arguments: session id = %s, timeout = %d", session_id, timeout);

        session_t *session = session_find_by_id(session_id);
        if (session) { /* existing session, continuing with new request */
            if (session->conn) { /* a different listen request for this session already exists */
                DEBUG_ESPQTCLIENT_CONN(conn, "a listen request for session %s already exists", session_id);
                session_respond(session);
            }

            session->conn = conn;
            session->timeout = timeout;
            session->access_level = access_level;
            DEBUG_ESPQTCLIENT_CONN(conn, "assigning listen request to existing session %s", session_id);

            if (session->queue_len) {
                DEBUG_ESPQTCLIENT_CONN(conn, "serving pending %d events", session->queue_len);
                session_respond(session);
            }
        }
        else { /* new session */
            session = session_create(session_id, conn, timeout, access_level);
            if (!session) { /* too many sessions */
                DEBUG_ESPQTCLIENT_CONN(conn, "too many sessions");
                respond_error(conn, 503, "busy");
                goto done;
            }
        }

        session_reset(session);
    }
    else { /* regular API call */
        api_conn_set(conn, access_level);

        int code;
        response_json = api_call_handle(method, path, query_json, request_json, &code);

        /* serve the HTML page only in setup mode and on any 404 */
        if (code == 404 && system_setup_mode_active()) {
            json_free(response_json);
            api_conn_reset();

            uint32 html_len;
            uint8 *html = html_load(&html_len);
            if (html) {
                respond_html(conn, 200, html, html_len);
                free(html);
            }
            else {
                respond_html(conn, 500, (uint8 *) "Error", 5);
            }
        }
        else if (response_json) {
            respond_json(conn, code, response_json);
            api_conn_reset();
        }
    }

    done:

    if (jwt_str) {
        free(jwt_str);
    }

    if (jwt) {
        jwt_free(jwt);
    }

    json_free(query_json);
    if (request_json) {
        json_free(request_json);
    }
}


void client_init(void) {
    int i;
    for (i = 0; i < MAX_PARALLEL_HTTP_REQ; i++) {
        http_state_array[i].slot_index = i;
    }

    DEBUG_ESPQTCLIENT("http server can handle %d parallel requests", MAX_PARALLEL_HTTP_REQ);

    tcp_server_init(device_tcp_port,
                    (tcp_conn_cb_t) on_tcp_conn,
                    (tcp_recv_cb_t) on_tcp_recv,
                    (tcp_sent_cb_t) on_tcp_sent,
                    (tcp_disc_cb_t) on_tcp_disc);

    httpclient_set_user_agent("espQToggle " FW_VERSION);
    httpserver_set_name(device_hostname);
}

void respond_json(struct espconn *conn, int status, json_t *json) {
    char *body;
    uint8 *response;

#if defined(_DEBUG) && defined(_DEBUG_ESPQTCLIENT)
    int free_mem_before_dump = system_get_free_heap_size();
#endif

    body = json_dump(json, /* also_free = */ TRUE);

#if defined(_DEBUG) && defined(_DEBUG_ESPQTCLIENT)
    int free_mem_after_dump = system_get_free_heap_size();
#endif

    int len = strlen(body);
    if (status == 204) {
        len = 0;  /* 204 No Content */
    }

    response = httpserver_build_response(status, JSON_CONTENT_TYPE,
                                         /* header_names = */ NULL,
                                         /* header_values = */ NULL,
                                         /* header_count = */ 0, (uint8 *) body, &len);

    if (status >= 400) {
        DEBUG_ESPQTCLIENT_CONN(conn, "responding with status %d: %s", status, body);
    }
    else {
        DEBUG_ESPQTCLIENT_CONN(conn, "responding with status %d", status);
    }

    free(body);
    tcp_send(conn, response, len, /* free on sent = */ TRUE);

#if defined(_DEBUG) && defined(_DEBUG_ESPQTCLIENT)
    int free_mem_after_send = system_get_free_heap_size();

    /* show free memory after sending the response */
    DEBUG_ESPQTCLIENT("free memory: before dump=%d, after dump=%d, after send=%d",
                      free_mem_before_dump, free_mem_after_dump, free_mem_after_send);
#endif
}

void respond_error(struct espconn *conn, int status, char *error) {
    json_t *json = json_obj_new();
    json_obj_append(json, "error", json_str_new(error));

    respond_json(conn, status, json);
}

void respond_html(struct espconn *conn, int status, uint8 *html, int len) {
    uint8 *response;

#if defined(_DEBUG) && defined(_DEBUG_ESPQTCLIENT)
    int free_mem_before_dump = system_get_free_heap_size();
#endif

#if defined(_DEBUG) && defined(_DEBUG_ESPQTCLIENT)
    int free_mem_after_dump = system_get_free_heap_size();
#endif

    static char *header_names[] = {"Content-Encoding"};
    static char *header_values[] = {"gzip"};

    response = httpserver_build_response(status, HTML_CONTENT_TYPE,
                                         header_names, header_values, 1, html, &len);

    DEBUG_ESPQTCLIENT_CONN(conn, "responding with status %d", status);

    tcp_send(conn, response, len, /* free on sent = */ TRUE);

#if defined(_DEBUG) && defined(_DEBUG_ESPQTCLIENT)
    int free_mem_after_send = system_get_free_heap_size();

    /* show free memory after sending the response */
    DEBUG_ESPQTCLIENT("free memory: before dump=%d, after dump=%d, after send=%d",
                      free_mem_before_dump, free_mem_after_dump, free_mem_after_send);
#endif
}
