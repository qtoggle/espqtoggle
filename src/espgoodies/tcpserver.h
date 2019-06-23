
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

#ifndef _ESPGOODIES_TCP_SERVER_H
#define _ESPGOODIES_TCP_SERVER_H


#include <ip_addr.h>
#include <espconn.h>

#define TCP_MAX_CONNECTIONS                     8
#define TCP_CONNECTION_TIMEOUT                  3600     /* seconds */


#if defined(_DEBUG) && defined(_DEBUG_TCPSERVER)
ICACHE_FLASH_ATTR void                          DEBUG_CONN(char *module, struct espconn *conn, char *fmt, ...);
#define DEBUG_TCPSERVER(fmt, ...)               DEBUG("[tcpserver     ] " fmt, ##__VA_ARGS__)
#define DEBUG_TCPSERVER_CONN(conn, fmt, ...)    DEBUG_CONN("tcpserver", conn, fmt, ##__VA_ARGS__)
#else
#define DEBUG_CONN(...)                         {}
#define DEBUG_TCPSERVER(...)                    {}
#define DEBUG_TCPSERVER_CONN(...)               {}
#endif



typedef void* (*tcp_conn_cb_t) (struct espconn* conn);
typedef void  (*tcp_recv_cb_t) (struct espconn* conn, void *app_info, char *data, int len);
typedef void  (*tcp_sent_cb_t) (struct espconn* conn, void *app_info);
typedef void  (*tcp_disc_cb_t) (struct espconn* conn, void *app_info);

ICACHE_FLASH_ATTR void          tcp_server_init(int tcp_port,
                                                tcp_conn_cb_t conn_cb,
                                                tcp_recv_cb_t recv_cb,
                                                tcp_sent_cb_t sent_cb,
                                                tcp_disc_cb_t disc_cb);

ICACHE_FLASH_ATTR void          tcp_server_stop();
ICACHE_FLASH_ATTR void          tcp_send(struct espconn *conn, uint8 *data, int len, bool free_on_sent);
ICACHE_FLASH_ATTR void          tcp_disconnect(struct espconn *conn);


#endif /* _ESPGOODIES_TCP_SERVER_H */

