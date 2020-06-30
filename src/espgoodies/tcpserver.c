
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
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <mem.h>

#include "common.h"
#include "tcpserver.h"


#define SEND_PACKET_SIZE 1024

typedef struct {

    void  *app_info;
    uint8 *send_buffer;
    int    send_len;
    int    send_offs;
    bool   free_on_sent;

} conn_info_t;

static struct espconn *tcp_server_conn = NULL;
static tcp_conn_cb_t   tcp_conn_cb = NULL;
static tcp_recv_cb_t   tcp_recv_cb = NULL;
static tcp_sent_cb_t   tcp_sent_cb = NULL;
static tcp_disc_cb_t   tcp_disc_cb = NULL;

ICACHE_FLASH_ATTR static void on_client_connection(void *arg);
ICACHE_FLASH_ATTR static void on_client_recv(void *arg, char *data, uint16 len);
ICACHE_FLASH_ATTR static void on_client_sent(void *arg);
ICACHE_FLASH_ATTR static void on_client_disconnect(void *arg);
ICACHE_FLASH_ATTR static void on_client_error(void *arg, int8 err);


#if defined(_DEBUG) && defined(_DEBUG_TCPSERVER)

void DEBUG_CONN(char *module, struct espconn *conn, char *fmt, ...) {
    char new_fmt[128];
    char buf[1024];

    va_list args;
    va_start(args, fmt);
    
    if (conn && conn->proto.tcp) {
        snprintf(new_fmt, sizeof(new_fmt), "[%-14s] [%d.%d.%d.%d:%d] %s",
                 module,
                 conn->proto.tcp->remote_ip[0],
                 conn->proto.tcp->remote_ip[1],
                 conn->proto.tcp->remote_ip[2],
                 conn->proto.tcp->remote_ip[3],
                 conn->proto.tcp->remote_port, fmt);
    }
    else {
        snprintf(new_fmt, sizeof(new_fmt), "[%-14s] [no connection] %s", module, fmt);
    }

    vsnprintf(buf, sizeof(buf), new_fmt, args);
    DEBUG("%s", buf);
    va_end(args);
}

#endif

void tcp_server_init(int tcp_port,
                     tcp_conn_cb_t conn_cb,
                     tcp_recv_cb_t recv_cb,
                     tcp_sent_cb_t sent_cb,
                     tcp_disc_cb_t disc_cb) {

    tcp_server_conn = (struct espconn *) zalloc(sizeof(struct espconn));

    espconn_create(tcp_server_conn);
    tcp_server_conn->type = ESPCONN_TCP;
    tcp_server_conn->state = ESPCONN_NONE;

    tcp_server_conn->proto.tcp = zalloc(sizeof(esp_tcp));
    tcp_server_conn->proto.tcp->local_port = tcp_port;

    espconn_regist_connectcb(tcp_server_conn, on_client_connection);
    espconn_accept(tcp_server_conn);
    espconn_regist_time(tcp_server_conn, 0, 0);
    espconn_tcp_set_max_con_allow(tcp_server_conn, TCP_MAX_CONNECTIONS);
    
    tcp_conn_cb = conn_cb;
    tcp_recv_cb = recv_cb;
    tcp_sent_cb = sent_cb;
    tcp_disc_cb = disc_cb;

    DEBUG_TCPSERVER("server listening on port %d", tcp_server_conn->proto.tcp->local_port);
}

void tcp_server_stop(void) {
    espconn_tcp_set_max_con_allow(tcp_server_conn, 0);
}

void tcp_send(struct espconn *conn, uint8 *data, int len, bool free_on_sent) {
    conn_info_t *info = NULL;
    if (conn->reverse) {
        info = conn->reverse;
    }

    /* Connections w/o info are not handled by the tcp server
     * and have most likely been refused by conn_cb;
     * data to such connections is sent, in one shot,
     * assuming its length is not above 1k */

    if (!info && len > SEND_PACKET_SIZE) {
        DEBUG_TCPSERVER_CONN(conn, "attempting to send %d bytes to unhandled tcp connection", len);
    }

    if (info && info->send_buffer) {
        DEBUG_TCPSERVER_CONN(conn, "refusing to send to tcp connection with pending data");
        return;
    }

    if (len <= SEND_PACKET_SIZE || !info) {
        DEBUG_TCPSERVER_CONN(conn, "sending a single packet of %d bytes", len);
        espconn_send(conn, data, len);
        if (free_on_sent) {
            free(data);
        }

        return;
    }

    if (free_on_sent) {
        info->send_buffer = data;
    }
    else {
        info->send_buffer = malloc(len);
        memcpy(info->send_buffer, data, len);
    }

    info->send_len = len;
    info->send_offs = SEND_PACKET_SIZE;
    info->free_on_sent = free_on_sent;

    int packet_count = len / SEND_PACKET_SIZE;
    if (len % SEND_PACKET_SIZE) {
        packet_count++;
    }
    DEBUG_TCPSERVER_CONN(conn, "sending packet %d/%d (%d/%d bytes)", 1, packet_count, SEND_PACKET_SIZE, len);
    if (espconn_send(conn, data, SEND_PACKET_SIZE)) {
        DEBUG_TCPSERVER_CONN(conn, "send failed");
    }
}

void tcp_disconnect(struct espconn *conn) {
    DEBUG_TCPSERVER_CONN(conn, "disconnecting");
    espconn_disconnect(conn);
}


void on_client_connection(void *arg) {
    struct espconn *conn = (struct espconn *) arg;

    espconn_regist_time(conn, TCP_CONNECTION_TIMEOUT, 1);
    
    DEBUG_TCPSERVER_CONN(conn, "new connection");

    void *app_info = tcp_conn_cb(conn);

    conn_info_t *info = zalloc(sizeof(conn_info_t));
    conn->reverse = info;
    info->app_info = app_info;

    espconn_regist_recvcb(conn, on_client_recv);
    espconn_regist_sentcb(conn, on_client_sent);
    espconn_regist_disconcb(conn, on_client_disconnect);
    espconn_regist_reconcb(conn, on_client_error);
}

void on_client_recv(void *arg, char *data, uint16 len) {
    struct espconn *conn = (struct espconn *) arg;
    if (conn->reverse) {
        conn_info_t *info = conn->reverse;
        tcp_recv_cb(conn, info->app_info, (uint8 *) data, len);
    }
}

void on_client_sent(void *arg) {
    struct espconn *conn = (struct espconn *) arg;

    if (!conn->reverse) {
        DEBUG_TCPSERVER_CONN(conn, "ignoring unhandled tcp connection");
        return;
    }

    conn_info_t *info = conn->reverse;
    if (!info->send_buffer) {
        tcp_sent_cb(conn, info);
        return;
    }

    int packet_count = info->send_len / SEND_PACKET_SIZE;
    if (info->send_len % SEND_PACKET_SIZE) {
        packet_count++;
    }

#if defined(_DEBUG) && defined(_DEBUG_TCPSERVER)
    int packet_no = info->send_offs / SEND_PACKET_SIZE + 1;
#endif

    int rem = info->send_len - info->send_offs;
    if (rem <= SEND_PACKET_SIZE) {
        DEBUG_TCPSERVER_CONN(conn, "sending packet %d/%d (%d/%d bytes)",
                             packet_no, packet_count, info->send_len, info->send_len);

        if (espconn_send(conn, info->send_buffer + info->send_offs, rem)) {
            DEBUG_TCPSERVER_CONN(conn, "send failed");
        }
        info->send_offs = info->send_len;
        free(info->send_buffer);
        info->send_buffer = NULL;
    }
    else {
        DEBUG_TCPSERVER_CONN(conn, "sending packet %d/%d (%d/%d bytes)",
                             packet_no, packet_count, SEND_PACKET_SIZE * packet_no, info->send_len);

        if (espconn_send(conn, info->send_buffer + info->send_offs, SEND_PACKET_SIZE)) {
            DEBUG_TCPSERVER_CONN(conn, "send failed");
            info->send_offs = info->send_len;
            free(info->send_buffer);
            info->send_buffer = NULL;
        }
        else {
            info->send_offs += SEND_PACKET_SIZE;
        }
    }
}

void on_client_disconnect(void *arg) {
    struct espconn *conn = (struct espconn *) arg;

    DEBUG_TCPSERVER_CONN(conn, "client disconnected");
    if (conn->reverse) {
        conn_info_t *info = conn->reverse;
        tcp_disc_cb(conn, info->app_info);
        if (info->send_buffer) {
            free(info->send_buffer);
        }
        free(info);
    }
    conn->reverse = NULL;
}

void on_client_error(void *arg, int8 err) {
    struct espconn *conn = (struct espconn *) arg;

    DEBUG_TCPSERVER_CONN(conn, "connection error");

    on_client_disconnect(conn);
}

