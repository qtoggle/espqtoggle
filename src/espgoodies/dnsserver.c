
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
 *
 * Inspired from https://github.com/esp8266/Arduino
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <mem.h>
#include <ip_addr.h>
#include <espconn.h>

#include "common.h"
#include "utils.h"
#include "wifi.h"
#include "dnsserver.h"


#define DNS_PORT                        53
#define DNS_TTL                         60

#define DNS_MAX_PACKET_SIZE             512
#define DNS_HEADER_SIZE                 sizeof(dns_header_t)

#define DNS_QR_QUERY                    0
#define DNS_QR_RESPONSE                 1
#define DNS_OP_CODE_QUERY               0

#define DNS_QCLASS_IN                   1
#define DNS_QCLASS_ANY                  255

#define DNS_QTYPE_A                     1
#define DNS_QTYPE_ANY                   255

#define DNS_REPLY_NO_ERROR              0
#define DNS_REPLY_FORM_ERROR            1
#define DNS_REPLY_SERVER_FAILURE        2
#define DNS_REPLY_NX_DOMAIN             3
#define DNS_REPLY_NOT_IMPLEMENTED       4
#define DNS_REPLY_REFUSED               5
#define DNS_REPLY_XY_DOMAIN             6
#define DNS_REPLY_XY_RRSET              7
#define DNS_REPLY_NX_RRSET              8


typedef struct {

    uint16                              id;  /* identification number */
    bool                                rd;  /* recursion desired */
    bool                                tc;  /* truncated message */
    bool                                aa;  /* authoritative answer */
    uint8                               op_code;  /* message type */
    bool                                qr;  /* query/response flag */
    uint8                               r_code;  /* response code */
    uint8                               z;  /* reserved */
    bool                                ra;  /* recursion available */
    uint16                              qd_count;  /* number of question entries */
    uint16                              an_count;  /* number of answer entries */
    uint16                              ns_count;  /* number of authority entries */
    uint16                              ar_count;  /* number of resource entries */

} dns_header_t;

static struct espconn                 * conn;
static int                              recv_so_far = 0;
static uint8                          * recv_buffer = NULL;
static ip_addr_t                        remote_ip;


ICACHE_FLASH_ATTR static void           on_client_recv(void *arg, char *data, uint16 len);
ICACHE_FLASH_ATTR static void           process(void);
ICACHE_FLASH_ATTR static uint8        * prepare_response(uint8 *request, uint16 *len);
ICACHE_FLASH_ATTR static uint8        * prepare_ip_response(dns_header_t *request_header, ip_addr_t ip_addr,
                                                            uint8 *query, uint16 query_len, uint16 *len);
ICACHE_FLASH_ATTR static uint8        * prepare_error_response(dns_header_t *request_header, uint8 reply_code,
                                                               uint8 *query, uint16 query_len, uint16 *len);


void dnsserver_start_captive(void) {
    /* initialize the connection */
    conn = zalloc(sizeof(struct espconn));

    conn->type = ESPCONN_UDP;
    conn->state = ESPCONN_NONE;
    conn->proto.udp = zalloc(sizeof(esp_udp));
    conn->proto.udp->local_port = DNS_PORT;

    int result = espconn_create(conn);
    if (result) {
        DEBUG_DNSSERVER("espconn_create() failed with result %d\n", result);
    }

    espconn_regist_recvcb(conn, on_client_recv);

    // TODO log
}

void dnsserver_stop(void) {
    espconn_delete(conn);
    conn = NULL;

    // TODO log
}


void on_client_recv(void *arg, char *data, uint16 len) {
    struct espconn *conn = (struct espconn *) arg;
    uint8 *addr_array = conn->proto.udp->remote_ip;
    IP4_ADDR(&remote_ip, addr_array[0], addr_array[1], addr_array[2], addr_array[3]);

    DEBUG_DNSSERVER("received %d bytes from " IPSTR, len, IP2STR(&remote_ip));

    // TODO handle multiple parallel clients
    recv_buffer = realloc(recv_buffer, recv_so_far + len);
    memcpy(recv_buffer, data, len);
    recv_so_far += len;

    // TODO handle client timeout
    process();
}

void process(void) {
    if (!recv_so_far) {
        return;
    }

    /* discard buffer if packet size larger than expected */
    if (recv_so_far > DNS_MAX_PACKET_SIZE) {
        DEBUG_DNSSERVER("received packet larger than %d", DNS_MAX_PACKET_SIZE);
        free(recv_buffer);
        recv_buffer = NULL;
        recv_so_far = 0;
    }

    /* discard buffer if packet size smaller than expected */
    // TODO dedup code
    if (recv_so_far > DNS_HEADER_SIZE) {
        DEBUG_DNSSERVER("received packet smaller than %d", DNS_HEADER_SIZE);
        free(recv_buffer);
        recv_buffer = NULL;
        recv_so_far = 0;
    }

    DEBUG_DNSSERVER("parsing request"); // TODO ...from IPSTR

    uint16 len = recv_so_far;
    uint8 *response = prepare_response(recv_buffer, &len);

    DEBUG_DNSSERVER("responding"); // TODO log
    // TODO dedup code
    free(recv_buffer);
    recv_buffer = NULL;
    recv_so_far = 0;

    static struct espconn response_conn;
    static esp_udp proto_udp;

    response_conn.type = ESPCONN_UDP;
    response_conn.state = ESPCONN_NONE;
    response_conn.proto.udp = &proto_udp;
    response_conn.proto.udp->remote_port = DNS_PORT;

    memcpy(proto_udp.remote_ip, &remote_ip, 4);

    espconn_create(&response_conn);
    espconn_send(&response_conn, response, len);
    espconn_delete(&response_conn);
}

uint8 *prepare_response(uint8 *request, uint16 *len) {
    uint8 *query, *start;
    uint32 rem, query_len, label_len;
    uint16 qtype, qclass;
    dns_header_t *header = (dns_header_t *) request;

    /* must be a query for us to do anything with it */
    if (header->qr != DNS_QR_QUERY) {
        return NULL;
    }

    /* if operation is anything other than query, we don't do it */
    if (header->op_code != DNS_OP_CODE_QUERY) {
        return prepare_error_response(header, DNS_REPLY_NOT_IMPLEMENTED, NULL, 0, len);
    }

    /* only support requests containing single queries */
    if (header->qd_count != htons(1)) {
        return prepare_error_response(header, DNS_REPLY_FORM_ERROR, NULL, 0, len);
    }

    if (header->an_count != 0 || header->ns_count != 0 || header->ar_count != 0) {
        return prepare_error_response(header, DNS_REPLY_FORM_ERROR, NULL, 0, len);
    }

    /* even if we're not going to use the query, we need to parse it
     * so we can check the address type that's being queried */

    query = start = request + DNS_HEADER_SIZE;
    rem = *len - DNS_HEADER_SIZE;
    while (rem != 0 && *start != 0) {
        label_len = *start;
        if (label_len + 1 > rem) {
            return prepare_error_response(header, DNS_REPLY_FORM_ERROR, NULL, 0, len);
        }

        rem -= (label_len + 1);
        start += (label_len + 1);
    }

    /* 1 byte label_length, 2 bytes qtype, 2 bytes qclass */
    if (rem < 5) {
        return prepare_error_response(header, DNS_REPLY_FORM_ERROR, NULL, 0, len);
    }

    /* skip the 0 length label that we found above */
    start += 1;

    memcpy(&qtype, start, sizeof(qtype));
    start += 2;
    memcpy(&qclass, start, sizeof(qclass));
    start += 2;

    query_len = start - query;
    if (qclass != htons(DNS_QCLASS_ANY) && qclass != htons(DNS_QCLASS_IN)) {
        return prepare_error_response(header, DNS_REPLY_FORM_ERROR, query, query_len, len);
    }

    if (qtype != htons(DNS_QTYPE_A) && qtype != htons(DNS_QTYPE_ANY)) {
        return prepare_error_response(header, DNS_REPLY_FORM_ERROR, query, query_len, len);
    }

    /* being a simple captive DNS resolver, reply with our IP */
    ip_addr_t ip_addr;
    IP4_ADDR(&ip_addr, WIFI_AP_IP1, WIFI_AP_IP2, WIFI_AP_IP3, WIFI_AP_IP4);

    return prepare_ip_response(header, ip_addr, query, query_len, len);
}

uint8 *prepare_ip_response(dns_header_t *request_header, ip_addr_t ip_addr,
                           uint8 *query, uint16 query_len, uint16 *len) {

    uint8 *response = malloc(DNS_HEADER_SIZE);
    dns_header_t *response_header = (dns_header_t *) response;
    memcpy(response_header, request_header, DNS_HEADER_SIZE);

    response_header->qr = DNS_QR_RESPONSE;
    response_header->r_code = DNS_REPLY_NO_ERROR;
    response_header->qd_count = htons(1);
    response_header->an_count = htons(1);
    response_header->ns_count = 0;
    response_header->ar_count = 0;

    response = realloc(response, DNS_HEADER_SIZE + query_len + 14);
    memcpy(response, query, query_len);
    *len = DNS_HEADER_SIZE + query_len;

    /* rather than restating the name here, we use a pointer to the name contained
     * in the query section; pointers have the top two bits set */
    uint16 value = htons(0xC000 | DNS_HEADER_SIZE);
    memcpy(response + *len, &value, 2); *len += 2;

    /* answer is type A (an IPv4 address) */
    value = htons(DNS_QTYPE_A);
    memcpy(response + *len, &value, 2); *len += 2;

    /* answer is in the Internet Class */
    value = htons(DNS_QCLASS_IN);
    memcpy(response + *len, &value, 2); *len += 2;

    /* TTL is already NBO */
    value = htons(DNS_TTL);
    memcpy(response + *len, &value, 2); *len += 2;

    /* length of RData is 4 bytes - IPv4 */
    value = htons(4);
    memcpy(response + *len, &value, 2); *len += 2;

    /* IP address itself */
    memcpy(response + *len, &ip_addr, 4); *len += 4;

    return response;
}

uint8 *prepare_error_response(dns_header_t *request_header, uint8 reply_code,
                              uint8 *query, uint16 query_len, uint16 *len) {

    uint8 *response = malloc(DNS_HEADER_SIZE);
    dns_header_t *response_header = (dns_header_t *) response;
    memcpy(response_header, request_header, DNS_HEADER_SIZE);

    response_header->qr = DNS_QR_RESPONSE;
    response_header->r_code = reply_code;
    if (query) {
        response_header->qd_count = htons(1);
    }
    else {
        response_header->qd_count = 0;
    }

    response_header->an_count = 0;
    response_header->ns_count = 0;
    response_header->ar_count = 0;

    response = realloc(response, DNS_HEADER_SIZE + query_len);
    memcpy(response, query, query_len);
    *len = DNS_HEADER_SIZE + query_len;

    return response;
}
