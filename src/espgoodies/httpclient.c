/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Martin d'Allens <martin.dallens@gmail.com> wrote this file. As long as you retain
 * this notice you can do whatever you want with this stuff. If we meet some day,
 * and you think this stuff is worth it, you can buy me a beer in return.
 * ----------------------------------------------------------------------------
 * copied from https://github.com/Caerbannog/esphttpclient
 */

#include <stdlib.h>
#include <osapi.h>
#include <user_interface.h>
#include <espconn.h>
#include <mem.h>
#include <limits.h>

#include "espgoodies/common.h"
#include "espgoodies/wifi.h"
#include "espgoodies/httpclient.h"


#define BUFFER_SIZE_MAX 8192 /* Size of http responses that will cause an error */


/* Internal request state */
typedef struct {

    struct espconn  *connection;
    char            *path;
    char            *method;
    uint8           *body;
    char            *headers;
    char            *hostname;
    char            *buffer;
    int32            body_len;
    int32            buffer_size;
    http_callback_t  callback;
    os_timer_t       timer;
    uint16           port;
    uint8            ip_addr[4];
    bool             secure;

} request_args_t;


static char *user_agent = NULL;


void ICACHE_FLASH_ATTR http_raw_request(
                           char *hostname,
                           uint16 port,
                           bool secure,
                           char *path,
                           char *method,
                           uint8 *body,
                           int body_len,
                           char *headers,
                           http_callback_t callback,
                           int timeout
                       );

static int  ICACHE_FLASH_ATTR chunked_decode(char *chunked, int size);
static void ICACHE_FLASH_ATTR receive_callback(void * arg, char * buf, uint16 len);
static void ICACHE_FLASH_ATTR sent_callback(void *arg);
static void ICACHE_FLASH_ATTR connect_callback(void *arg);
static void ICACHE_FLASH_ATTR disconnect_callback(void * arg);
static void ICACHE_FLASH_ATTR error_callback(void *arg, int8 errType);
static void ICACHE_FLASH_ATTR dns_callback(const char * hostname, ip_addr_t *addr, void * arg);
static void ICACHE_FLASH_ATTR timeout_callback(void *arg);


void http_raw_request(
    char *hostname,
    uint16 port,
    bool secure,
    char *path,
    char *method,
    uint8 *body,
    int body_len,
    char *headers,
    http_callback_t callback,
    int timeout
) {
    DEBUG_HTTPCLIENT("DNS request");

    request_args_t *req = (request_args_t *)malloc(sizeof(request_args_t));
    req->hostname = strdup(hostname);
    req->path = strdup(path);
    req->port = port;
    req->secure = secure;
    req->headers = headers;
    req->method = (char *) method;
    req->buffer_size = 1;
    req->buffer = (char *)malloc(1);
    req->buffer[0] = 0;
    req->callback = callback;
    req->body_len = body_len;
    req->body = malloc(body_len + 1);
    memcpy(req->body, body, body_len);
    req->body[body_len] = '\0'; /* Null-terminate body so that we can log it as a string */

    ip_addr_t addr;
    err_t error = espconn_gethostbyname((struct espconn *)req, // It seems we don't need a real espconn pointer here.
                                        hostname, &addr, dns_callback);

    if (error == ESPCONN_INPROGRESS) {
        DEBUG_HTTPCLIENT("DNS pending");
    }
    else if (error == ESPCONN_OK) {
        /* Already in the local names table (or hostname was an IP address), execute the callback ourselves */
        dns_callback(hostname, &addr, req);
    }
    else {
        if (error == ESPCONN_ARG) {
            DEBUG_HTTPCLIENT("DNS arg error %s", hostname);
        }
        else {
            DEBUG_HTTPCLIENT("DNS error code %d", error);
        }
        dns_callback(hostname, NULL, req); /* Handle all DNS errors the same way */
    }

    /* Timeout timer */
    os_timer_disarm(&req->timer);
    os_timer_setfn(&req->timer, timeout_callback, req);
    os_timer_arm(&req->timer, timeout * 1000, /* repeat = */ FALSE);
}

int chunked_decode(char *chunked, int size) {
    char *src = chunked;
    char *end = chunked + size;
    int i, dst = 0;
    do {
        //[chunk-size]
        i = strtol(src, NULL, 16);
        DEBUG_HTTPCLIENT("chunk size:%d\r", i);
        if (i <= 0)
            break;
        //[chunk-size-end-ptr]
        src = (char *)os_strstr(src, "\r\n") + 2;
        //[chunk-data]
        os_memmove(&chunked[dst], src, i);
        src += i + 2; /* CRLF */
        dst += i;
    } while (src < end);
    //
    //footer CRLF
    //

    /* Decoded size */
    return dst;
}

void receive_callback(void * arg, char * buf, uint16 len) {
    struct espconn *conn = (struct espconn *) arg;
    request_args_t *req = (request_args_t *) conn->reverse;

    if (req->buffer == NULL) {
        return;  /* The disconnect callback will be called */
    }

    /* Do the equivalent of a realloc() */
    const int new_size = req->buffer_size + len;
    char * new_buffer;
    if (new_size > BUFFER_SIZE_MAX || NULL == (new_buffer = (char *) malloc(new_size))) {
        DEBUG_HTTPCLIENT("response too long (%d)", new_size);
        req->buffer[0] = '\0';  /* Discard the buffer to avoid using an incomplete response */
#ifdef _SSL
        if (req->secure)
            espconn_secure_disconnect(conn);
        else
#endif
        espconn_disconnect(conn);

        return;  /* The disconnect callback will be called */
    }

    os_memcpy(new_buffer, req->buffer, req->buffer_size);
    os_memcpy(new_buffer + req->buffer_size - 1 /* Overwrite the null character */, buf, len);  /* Append new data */
    new_buffer[new_size - 1] = '\0';  /* Make sure there is an end of string */

    free(req->buffer);
    req->buffer = new_buffer;
    req->buffer_size = new_size;
}

void sent_callback(void *arg) {
    struct espconn *conn = (struct espconn *) arg;
    request_args_t *req = (request_args_t *) conn->reverse;

    if (!req->body) {
        DEBUG_HTTPCLIENT("all sent");
    }
    else {
        /* The head was sent, now send the body */

        DEBUG_HTTPCLIENT("request body (%d bytes):\n----------------\n%s\n----------------", req->body_len, req->body);

#ifdef _SSL
        if (req->secure)
            espconn_secure_sent(conn, req->body, req->body_len));
        else
#endif
        espconn_sent(conn, req->body, req->body_len);

        free(req->body);
        req->body = NULL;
    }
}

void connect_callback(void *arg) {
    struct espconn *conn = (struct espconn *) arg;
    request_args_t *req = (request_args_t *) conn->reverse;

    DEBUG_HTTPCLIENT("connected");

    espconn_regist_recvcb(conn, receive_callback);
    espconn_regist_sentcb(conn, sent_callback);

    int len = strlen(req->method) + strlen(req->path) + strlen(req->headers) + 64;
    char buf[len];
    len = snprintf(
        buf,
        len,
        "%s %s HTTP/1.1\r\n"
        "%s\r\n",
        req->method,
        req->path,
        req->headers
    );

    if (!req->body) {
        DEBUG_HTTPCLIENT("request (%d bytes):\n----------------\n%s----------------", len, buf);
    }
    else {
        DEBUG_HTTPCLIENT("request head (%d bytes):\n----------------\n%s----------------", len, buf);
    }

#ifdef _SSL
    if (req->secure)
        espconn_secure_sent(conn, (uint8 *) buf, len);
    else
#endif
        espconn_sent(conn, (uint8 *) buf, len);

    free(req->headers);
    req->headers = NULL;
    DEBUG_HTTPCLIENT("sending request header");
}

void disconnect_callback(void * arg) {
    DEBUG_HTTPCLIENT("disconnected");
    struct espconn *conn = (struct espconn *)arg;

    if (conn == NULL) {
        return;
    }

    if (conn->reverse != NULL) {
        request_args_t * req = (request_args_t *) conn->reverse;
        int i, http_status = -1;
        int body_size = 0;
        int header_count = 0;
        char *body = "";
        char **header_names = NULL;
        char **header_values = NULL;

        if (req->buffer == NULL) {
            DEBUG_HTTPCLIENT("buffer shouldn't be NULL");
        }
        else if (req->buffer[0] != '\0') {
            // FIXME: make sure this is not a partial response, using the Content-Length header.
            const char *version10 = "HTTP/1.0 ";
            const char *version11 = "HTTP/1.1 ";
            if (os_strncmp(req->buffer, version10, strlen(version10)) != 0 &&
                os_strncmp(req->buffer, version11, strlen(version11)) != 0) {
                DEBUG_HTTPCLIENT("invalid version in %s", req->buffer);
            }
            else {
                http_status = strtol(req->buffer + strlen(version10), NULL, 10);
                /* Find body and zero terminate headers */
                body = (char *)os_strstr(req->buffer, "\r\n\r\n") + 2;
                *body++ = '\0';
                *body++ = '\0';

                body_size = req->buffer_size - (body - req->buffer);

                if(os_strstr(req->buffer, "Transfer-Encoding: chunked"))
                {
                    body_size = chunked_decode(body, body_size);
                    body[body_size] = '\0';
                }
            }
        }

        /* Parse req->buffer which contains raw headers */

        i = 0;
        while (req->buffer[i] && req->buffer[i] != '\n') {  /* Skip response line */
            i++;
        }

        if (req->buffer[i]) {  /* Skip \n */
            i++;
        }

        while (req->buffer[i]) {
            header_names = realloc(header_names, sizeof(char *) * (header_count + 1));
            header_names[header_count] = req->buffer + i;

            while (req->buffer[i] && req->buffer[i] != ':') {  /* Skip name */
                i++;
            }
            if (!req->buffer[i]) {
                break;  /* Unexpected headers end */
            }
            req->buffer[i++] = 0;  /* null terminate header name */

            while (req->buffer[i] == ' ') {  /* Skip spaces */
                i++;
            }

            header_values = realloc(header_values, sizeof(char *) * (header_count + 1));
            header_values[header_count] = req->buffer + i;

            while (req->buffer[i] && req->buffer[i] != '\r' && req->buffer[i] != '\n') {  /* Skip value */
                i++;
            }
            if (!req->buffer[i]) {
                break;  /* Unexpected headers end */
            }

            req->buffer[i++] = 0;  /* null terminate header name */
            i++;  /* Skip \n */

            header_count++;
        }

        if (req->callback) {
            req->callback(body, body_size, http_status, header_names, header_values, header_count, req->ip_addr);
        }

        /* Make sure we won't fire the timeout timer anymore */
        os_timer_disarm(&req->timer);

        free(header_names);
        free(header_values);

        free(req->buffer);
        free(req->body);
        free(req->headers);
        free(req->path);
        free(req->hostname);
        free(req);
    }

    if(conn->proto.tcp != NULL) {
        free(conn->proto.tcp);
        conn->proto.tcp = NULL;
    }

    espconn_delete(conn);

    free(conn);
}

void error_callback(void *arg, int8 errType) {
    DEBUG_HTTPCLIENT("disconnected with error");
    disconnect_callback(arg);
}

void dns_callback(const char * hostname, ip_addr_t *addr, void * arg) {
    request_args_t * req = (request_args_t *)arg;

    if (addr == NULL) {
        DEBUG_HTTPCLIENT("DNS failed for %s", hostname);
        if (req->callback) {
            req->callback("", 0, HTTP_STATUS_DNS_ERROR, NULL, NULL, 0, NULL);
        }

        os_timer_disarm(&req->timer);

        free(req->buffer);
        free(req->body);
        free(req->headers);
        free(req->path);
        free(req->hostname);
        free(req);
    }
    else {
        DEBUG_HTTPCLIENT("DNS found %s -> " WIFI_IP_FMT, hostname, IP2STR(addr));

        struct espconn * conn = (struct espconn *)malloc(sizeof(struct espconn));
        conn->type = ESPCONN_TCP;
        conn->state = ESPCONN_NONE;
        conn->proto.tcp = (esp_tcp *) malloc(sizeof(esp_tcp));
        conn->proto.tcp->local_port = espconn_port();
        conn->proto.tcp->remote_port = req->port;
        conn->reverse = req;

        req->connection = conn;
        req->ip_addr[0] = ip4_addr1(addr);
        req->ip_addr[1] = ip4_addr2(addr);
        req->ip_addr[2] = ip4_addr3(addr);
        req->ip_addr[3] = ip4_addr4(addr);

        os_memcpy(conn->proto.tcp->remote_ip, addr, 4);

        espconn_regist_connectcb(conn, connect_callback);
        espconn_regist_disconcb(conn, disconnect_callback);
        espconn_regist_reconcb(conn, error_callback);

#ifdef _SSL
        if (req->secure) {
            espconn_secure_set_size(ESPCONN_CLIENT, 5120);  /* Set SSL buffer size */
            espconn_secure_connect(conn);
        } else
#endif

        espconn_connect(conn);
    }
}

void timeout_callback(void *arg) {
    request_args_t * req = (request_args_t *)arg;

    DEBUG_HTTPCLIENT("request timeout waiting for %s:%d", req->hostname, req->port);
    if (req->callback) {
        req->callback("", 0, HTTP_STATUS_TIMEOUT, NULL, NULL, 0, NULL);
    }

    req->callback = NULL;
    if (req->buffer[0] != 0) {  /* Prevents further lookup through body when handling disconnect */
        req->buffer[0] = 0;
    }

#ifdef _SSL
        if (req->secure)
            espconn_secure_disconnect(req->connection);
        else
#endif
        espconn_disconnect(req->connection);
}


void httpclient_set_user_agent(char *agent) {
    free(user_agent);
    user_agent = strdup(agent);

    DEBUG_HTTPCLIENT("user agent set to \"%s\"", agent);
}

void httpclient_request(
    char *method,
    char *url,
    uint8 *body,
    int body_len,
    char *header_names[],
    char *header_values[],
    int header_count,
    http_callback_t callback,
    int timeout
) {
    char hostname[128] = "";
    char *h, *headers = NULL;
    int i, hl, headers_len = 0;
    uint16 port = 80;
    bool secure = FALSE;

    bool is_http  = os_strncmp(url, "http://",  strlen("http://"))  == 0;
    bool is_https = os_strncmp(url, "https://", strlen("https://")) == 0;

    if (is_http)
        url += strlen("http://"); // Get rid of the protocol.
    else if (is_https) {
        port = 443;
        secure = TRUE;
        url += strlen("https://"); // Get rid of the protocol.
    } else {
        DEBUG_HTTPCLIENT("URL is not HTTP or HTTPS %s", url);
        return;
    }

    char * path = os_strchr(url, '/');
    if (path == NULL) {
        path = os_strchr(url, '\0'); // Pointer to end of string.
    }

    char * colon = os_strchr(url, ':');
    if (colon > path) {
        colon = NULL; // Limit the search to characters before the path.
    }

    if (colon == NULL) { // The port is not present.
        os_memcpy(hostname, url, path - url);
        hostname[path - url] = '\0';
    }
    else {
        port = strtol(colon + 1, NULL, 10);
        if (port == 0) {
            DEBUG_HTTPCLIENT("port error %s", url);
            return;
        }

        os_memcpy(hostname, url, colon - url);
        hostname[colon - url] = '\0';
    }


    if (path[0] == 0) { /* Empty path is not allowed */
        path = "/";
    }

    /* Add required Host header */
    char host_value[strlen(hostname) + 7];
    snprintf(host_value, sizeof(host_value), "%s:%d", hostname, port);

    hl = strlen(host_value) + 8;  /* Host: value\r\n */
    headers_len += hl;
    headers = realloc(headers, headers_len + 1);
    h = headers + headers_len - hl;
    snprintf(h, hl + 1, "Host: %s\r\n", host_value);

    /* Add required Connection header */
    hl = 19;  /* Connection: close\r\n */
    headers_len += hl;
    headers = realloc(headers, headers_len + 1);
    h = headers + headers_len - hl;
    snprintf(h, hl + 1, "Connection: close\r\n");

    /* Add required User-Agent header */
    hl = strlen(user_agent) + 14;  /* User-Agent: value\r\n */
    headers_len += hl;
    headers = realloc(headers, headers_len + 1);
    h = headers + headers_len - hl;
    snprintf(h, hl + 1, "User-Agent: %s\r\n", user_agent);

    if (body_len) {
        /* Add required Content-Length header */
        char cl_value[16];
        snprintf(cl_value, sizeof(cl_value), "%d", body_len);

        hl = strlen(cl_value) + 18;  /* Content-Length: value\r\n */
        headers_len += hl;
        headers = realloc(headers, headers_len + 1);
        h = headers + headers_len - hl;
        snprintf(h, hl + 1, "Content-Length: %s\r\n", cl_value);

        /* Add extra headers */
        for (i = 0; i < header_count; i++) {
            hl = strlen(header_names[i]) + strlen(header_values[i]) + 4;
            headers_len += hl;
            headers = realloc(headers, headers_len + 1);
            h = headers + headers_len - hl;
            snprintf(h, hl + 1, "%s: %s\r\n", header_names[i], header_values[i]);
        }
    }

    headers[headers_len] = 0;

    http_raw_request(hostname, port, secure, path, method, body, body_len, headers, callback, timeout);
}
