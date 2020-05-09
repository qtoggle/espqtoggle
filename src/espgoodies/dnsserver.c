
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

#define DNS_FLAGS_TYPE_MASK             0x8000
#define DNS_FLAGS_TYPE_SHIFT            15
#define DNS_FLAGS_OP_CODE_MASK          0x7800
#define DNS_FLAGS_OP_CODE_SHIFT         11
#define DNS_FLAGS_REPLY_CODE_MASK       0x000F
#define DNS_FLAGS_REPLY_CODE_SHIFT      0

#define DNS_QR_QUERY                    0
#define DNS_QR_RESPONSE                 1
#define DNS_OP_CODE_QUERY               0

#define DNS_QCLASS_IN                   1
#define DNS_QCLASS_ANY                  255

#define DNS_QTYPE_A                     1
#define DNS_QTYPE_ANY                   255

#define DNS_REPLY_NO_ERROR              0
#define DNS_REPLY_FORMAT_ERROR          1
#define DNS_REPLY_SERVER_FAILURE        2
#define DNS_REPLY_NX_DOMAIN             3
#define DNS_REPLY_NOT_IMPLEMENTED       4
#define DNS_REPLY_REFUSED               5
#define DNS_REPLY_XY_DOMAIN             6
#define DNS_REPLY_XY_RRSET              7
#define DNS_REPLY_NX_RRSET              8


typedef struct {

    uint16                              id;  /* Identification number */
    uint16                              flags;
    uint16                              qd_count;  /* Number of question entries */
    uint16                              an_count;  /* Number of answer entries */
    uint16                              ns_count;  /* Number of authority entries */
    uint16                              ar_count;  /* Number of resource entries */

} dns_header_t;

static struct espconn                 * conn;
static bool                             started = FALSE;


ICACHE_FLASH_ATTR static void           listen(void);
ICACHE_FLASH_ATTR static void           reset(void);
ICACHE_FLASH_ATTR static void           on_client_recv(void *arg, char *data, uint16 len);
ICACHE_FLASH_ATTR static void           process_request(uint8 *request, uint16 len,
                                                        uint8 remote_ip[], uint16 remote_port);
ICACHE_FLASH_ATTR static uint8        * prepare_response(uint8 *request, uint16 *len);
ICACHE_FLASH_ATTR static uint8        * prepare_ip_response(dns_header_t *request_header, ip_addr_t ip_addr,
                                                            uint8 *query, uint16 query_len, uint16 *len);
ICACHE_FLASH_ATTR static uint8        * prepare_error_response(dns_header_t *request_header, uint8 reply_code,
                                                               uint8 *query, uint16 query_len, uint16 *len);


void dnsserver_start_captive(void) {
    if (started) {
        return;
    }

    started = TRUE;
    DEBUG_DNSSERVER("started");

    listen();
}

void dnsserver_stop(void) {
    if (!started) {
        return;
    }

    if (conn) {
        reset();
    }

    started = FALSE;
    DEBUG_DNSSERVER("stopped");
}


void listen(void) {
    /* Initialize the connection */
    conn = zalloc(sizeof(struct espconn));

    conn->type = ESPCONN_UDP;
    conn->state = ESPCONN_NONE;
    conn->proto.udp = zalloc(sizeof(esp_udp));
    conn->proto.udp->local_port = DNS_PORT;

    int result = espconn_create(conn);
    if (result) {
        DEBUG_DNSSERVER("espconn_create() failed with result %d\n", result);
        reset();
        return;
    }

    espconn_regist_recvcb(conn, on_client_recv);

    DEBUG_DNSSERVER("listening on port %d", DNS_PORT);
}

void reset(void) {
    if (conn) {
        espconn_delete(conn);
        free(conn->proto.udp);
        free(conn);
        conn = NULL;
    }
}

void on_client_recv(void *arg, char *data, uint16 len) {
    struct espconn *conn = (struct espconn *) arg;
    remot_info *remote_info;

    espconn_get_connection_info(conn, &remote_info, 0);
    DEBUG_DNSSERVER("received %d bytes from " IP_FMT ":%d", len,
                    IP2STR(remote_info->remote_ip), remote_info->remote_port);

    process_request((uint8 *) data, len, remote_info->remote_ip, remote_info->remote_port);
}

void process_request(uint8 *request, uint16 len, uint8 remote_ip[], uint16 remote_port) {
    /* Verify packet size */
    if (len > DNS_MAX_PACKET_SIZE || len < DNS_HEADER_SIZE) {
        DEBUG_DNSSERVER("received packet of invalid size %d", len);
        reset();
        return;
    }

    DEBUG_DNSSERVER("parsing request from " IP_FMT ":%d", IP2STR(remote_ip), remote_port);

    uint8 *response = prepare_response(request, &len);
    if (!response) {
        reset();
        return;
    }

    DEBUG_DNSSERVER("responding to " IP_FMT ":%d", IP2STR(remote_ip), remote_port);

    /* We must close the incoming connection before being able to open the outgoing one */
    reset();

    struct espconn *response_conn = zalloc(sizeof(struct espconn));
    esp_udp *proto_udp = zalloc(sizeof(esp_udp));

    response_conn->type = ESPCONN_UDP;
    response_conn->state = ESPCONN_NONE;
    response_conn->proto.udp = proto_udp;
    response_conn->proto.udp->local_port = DNS_PORT;
    response_conn->proto.udp->remote_port = remote_port;

    memcpy(proto_udp->remote_ip, remote_ip, 4);

    espconn_create(response_conn);
    espconn_send(response_conn, response, len);
    espconn_delete(response_conn);

    free(proto_udp);
    free(response_conn);
    free(response);

    /* Start listening for the next request */
    listen();
}

uint8 *prepare_response(uint8 *request, uint16 *len) {
    uint8 *query, *start;
    uint32 rem, query_len, label_len;
    uint16 qtype, qclass;
    dns_header_t *header = (dns_header_t *) request;

    /* Must be a query for us to do anything with it */
    header->flags = ntohs(header->flags);
    if (((header->flags & DNS_FLAGS_TYPE_MASK) >> DNS_FLAGS_TYPE_SHIFT) != DNS_QR_QUERY) {
        DEBUG_DNSSERVER("ignoring invalid DNS request");
        return NULL;
    }

    /* If operation is anything other than query, we don't do it */
    if (((header->flags & DNS_FLAGS_OP_CODE_MASK) >> DNS_FLAGS_OP_CODE_SHIFT) != DNS_OP_CODE_QUERY) {
        DEBUG_DNSSERVER("responding with not implemented (not a query)");
        return prepare_error_response(header, DNS_REPLY_NOT_IMPLEMENTED, NULL, 0, len);
    }

    /* Only support requests containing single queries */
    if (header->qd_count != htons(1)) {
        DEBUG_DNSSERVER("responding with format error (more than one query)");
        return prepare_error_response(header, DNS_REPLY_FORMAT_ERROR, NULL, 0, len);
    }

    if (header->an_count != 0 || header->ns_count != 0 || header->ar_count != 0) {
        DEBUG_DNSSERVER("responding with format error (non-null counters)");
        return prepare_error_response(header, DNS_REPLY_FORMAT_ERROR, NULL, 0, len);
    }

    /* Find the query */
    query = start = request + DNS_HEADER_SIZE;
    rem = *len - DNS_HEADER_SIZE;
    char query_str[256] = {0};
    uint16 query_str_len = 0;
    while (rem != 0 && *start != 0) {
        label_len = *start;
        if (label_len + 1 > rem) {
            DEBUG_DNSSERVER("responding with format error (unterminated label)");
            return prepare_error_response(header, DNS_REPLY_FORMAT_ERROR, NULL, 0, len);
        }

        memcpy(query_str + query_str_len, start + 1, label_len);
        query_str[query_str_len + label_len] = '.';
        query_str[query_str_len + label_len + 1] = 0;
        query_str_len += label_len + 1;

        rem -= (label_len + 1);
        start += (label_len + 1);
    }

    DEBUG_DNSSERVER("requested lookup \"%s\"", query_str);

    /* 1 byte label length, 2 bytes qtype, 2 bytes qclass */
    if (rem < 5) {
        DEBUG_DNSSERVER("responding with format error (unexpected remaining bytes)");
        return prepare_error_response(header, DNS_REPLY_FORMAT_ERROR, NULL, 0, len);
    }

    /* Skip the 0 length label that we found above */
    start += 1;

    memcpy(&qtype, start, sizeof(qtype));
    start += 2;
    memcpy(&qclass, start, sizeof(qclass));
    start += 2;

    query_len = start - query;
    if (qclass != htons(DNS_QCLASS_ANY) && qclass != htons(DNS_QCLASS_IN)) {
        DEBUG_DNSSERVER("responding with non-existent domain (invalid qclass)");
        return prepare_error_response(header, DNS_REPLY_NX_DOMAIN, query, query_len, len);
    }

    if (qtype != htons(DNS_QTYPE_A) && qtype != htons(DNS_QTYPE_ANY)) {
        DEBUG_DNSSERVER("responding with non-existent domain (invalid qtype)");
        return prepare_error_response(header, DNS_REPLY_NX_DOMAIN, query, query_len, len);
    }

    /* Being a simple captive DNS resolver, reply with our IP */
    struct ip_info ip_info;
    wifi_get_ip_info(SOFTAP_IF, &ip_info);

    return prepare_ip_response(header, ip_info.ip, query, query_len, len);
}

uint8 *prepare_ip_response(dns_header_t *request_header, ip_addr_t ip_addr,
                           uint8 *query, uint16 query_len, uint16 *len) {

    uint8 *response = malloc(DNS_HEADER_SIZE + query_len + 16);
    uint8 *response_p = response + DNS_HEADER_SIZE;
    dns_header_t *response_header = (dns_header_t *) response;

    memcpy(response_header, request_header, DNS_HEADER_SIZE);

    response_header->flags &= ~(DNS_FLAGS_TYPE_MASK << DNS_FLAGS_TYPE_SHIFT);
    response_header->flags &= ~(DNS_FLAGS_REPLY_CODE_MASK << DNS_FLAGS_REPLY_CODE_SHIFT);
    response_header->flags |= DNS_QR_RESPONSE << DNS_FLAGS_TYPE_SHIFT;
    response_header->flags |= DNS_REPLY_NO_ERROR << DNS_FLAGS_REPLY_CODE_SHIFT;
    response_header->flags = htons(response_header->flags);
    response_header->qd_count = htons(1);
    response_header->an_count = htons(1);
    response_header->ns_count = 0;
    response_header->ar_count = 0;
    *len = DNS_HEADER_SIZE;

    memcpy(response_p, query, query_len);
    *len += query_len;
    response_p += query_len;

    /* Rather than restating the name here, we use a pointer to the name contained in the query section; pointers have
     * the top two bits set */
    uint16 svalue = htons(0xC000 | DNS_HEADER_SIZE);
    uint32 lvalue;
    memcpy(response_p, &svalue, 2);
    *len += 2;
    response_p += 2;

    /* Answer is type A (an IPv4 address) */
    svalue = htons(DNS_QTYPE_A);
    memcpy(response_p, &svalue, 2);
    *len += 2;
    response_p += 2;

    /* Answer is in the Internet Class */
    svalue = htons(DNS_QCLASS_IN);
    memcpy(response_p, &svalue, 2);
    *len += 2;
    response_p += 2;

    /* TTL is already NBO */
    lvalue = htonl(DNS_TTL);
    memcpy(response_p, &lvalue, 4);
    *len += 4;
    response_p += 4;

    /* Length of RData is 4 bytes - IPv4 */
    svalue = htons(4);
    memcpy(response_p, &svalue, 2);
    *len += 2;
    response_p += 2;

    /* IP address itself */
    memcpy(response_p, &ip_addr, 4);
    *len += 4;

    return response;
}

uint8 *prepare_error_response(dns_header_t *request_header, uint8 reply_code,
                              uint8 *query, uint16 query_len, uint16 *len) {

    uint8 *response = malloc(DNS_HEADER_SIZE + query_len);
    uint8 *response_p = response + DNS_HEADER_SIZE;
    dns_header_t *response_header = (dns_header_t *) response;

    memcpy(response_header, request_header, DNS_HEADER_SIZE);

    response_header->flags &= ~(DNS_FLAGS_TYPE_MASK << DNS_FLAGS_TYPE_SHIFT);
    response_header->flags &= ~(DNS_FLAGS_REPLY_CODE_MASK << DNS_FLAGS_REPLY_CODE_SHIFT);
    response_header->flags |= DNS_QR_RESPONSE << DNS_FLAGS_TYPE_SHIFT;
    response_header->flags |= reply_code << DNS_FLAGS_REPLY_CODE_SHIFT;
    response_header->flags = htons(response_header->flags);
    if (query) {
        response_header->qd_count = htons(1);
    }
    else {
        response_header->qd_count = 0;
    }

    response_header->an_count = 0;
    response_header->ns_count = 0;
    response_header->ar_count = 0;
    *len = DNS_HEADER_SIZE;

    memcpy(response_p, query, query_len);
    *len += query_len;
    response_p += query_len;

    return response;
}
