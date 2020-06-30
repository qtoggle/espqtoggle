/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Martin d'Allens <martin.dallens@gmail.com> wrote this file. As long as you retain
 * this notice you can do whatever you want with this stuff. If we meet some day,
 * and you think this stuff is worth it, you can buy me a beer in return.
 * ----------------------------------------------------------------------------
 * copied from https://github.com/Caerbannog/esphttpclient
 */

#ifndef _ESPGOODIES_HTTPCLIENT_H
#define _ESPGOODIES_HTTPCLIENT_H


#ifdef _DEBUG_HTTPCLIENT
#define DEBUG_HTTPCLIENT(fmt, ...) DEBUG("[httpclient    ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_HTTPCLIENT(...)      {}
#endif

#define HTTP_STATUS_DNS_ERROR 590
#define HTTP_STATUS_TIMEOUT   591

#define HTTP_DEF_TIMEOUT      20

typedef void (*http_callback_t)(
    char *body,
    int body_len,
    int status,
    char *header_names[],
    char *header_values[],
    int header_count,
    uint8 ip_addr[]
);

void ICACHE_FLASH_ATTR httpclient_set_user_agent(char *agent);
void ICACHE_FLASH_ATTR httpclient_request(
                           char *method,
                           char *url,
                           uint8 *body,
                           int body_len,
                           char *header_names[],
                           char *header_values[],
                           int header_count,
                           http_callback_t callback,
                           int timeout
                       );

#endif /* _ESPGOODIES_HTTPCLIENT_H */
