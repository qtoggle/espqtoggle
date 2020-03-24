
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

#ifndef _ESPGOODIES_WIFI_H
#define _ESPGOODIES_WIFI_H

#include <ip_addr.h>
#include <user_interface.h>


#ifdef _DEBUG_WIFI
#define DEBUG_WIFI(fmt, ...)            DEBUG("[wifi          ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_WIFI(...)                 {}
#endif

#define BSSID_FMT                       "%02X:%02X:%02X:%02X:%02X:%02X"
#define BSSID2STR(bssid)                (bssid[0]), (bssid[1]), (bssid[2]), (bssid[3]), (bssid[4]), (bssid[5])

#define WIFI_SCAN_INTERVAL_MIN          1       /* seconds */
#define WIFI_SCAN_INTERVAL_MAX          3600    /* seconds */

#define WIFI_SCAN_THRESH_MIN            1       /* RSSI dBm */
#define WIFI_SCAN_THRESH_MAX            50      /* RSSI dBm */
#define WIFI_SCAN_THRESH_DEF            15      /* RSSI dBm */

#define WIFI_EVENT_CONNECTED            1
#define WIFI_EVENT_DISCONNECTED         2
#define WIFI_EVENT_CONNECT_TIMEOUT      3

#define WIFI_SSID_MAX_LEN               32
#define WIFI_BSSID_LEN                  6
#define WIFI_PSK_MAX_LEN                64


    /* IP address */
#ifndef WIFI_AP_IP1
#define WIFI_AP_IP1                     192
#define WIFI_AP_IP2                     168
#define WIFI_AP_IP3                     5
#define WIFI_AP_IP4                     1
#endif

    /* netmask */
#ifndef WIFI_AP_NM
#define WIFI_AP_NM1                     255
#define WIFI_AP_NM2                     255
#define WIFI_AP_NM3                     255
#define WIFI_AP_NM4                     0
#endif

    /* start DHCP IP address */
#ifndef WIFI_AP_SI1
#define WIFI_AP_SI1                     192
#define WIFI_AP_SI2                     168
#define WIFI_AP_SI3                     5
#define WIFI_AP_SI4                     100
#endif

    /* end DHCP IP address */
#ifndef WIFI_AP_EI1
#define WIFI_AP_EI1                     192
#define WIFI_AP_EI2                     168
#define WIFI_AP_EI3                     5
#define WIFI_AP_EI4                     150
#endif

#ifndef WIFI_AP_PSK
#define WIFI_AP_PSK                     NULL
#endif


typedef struct {

    uint8       bssid[WIFI_BSSID_LEN];
    char        ssid[WIFI_SSID_MAX_LEN];
    int         channel;
    int         rssi;
    int         auth_mode;

} wifi_scan_result_t;


/* results is an array that must be freed() in callback! */
typedef void (* wifi_scan_callback_t)(wifi_scan_result_t *results, int len);
typedef void (* wifi_connect_callback_t)(bool connected);


ICACHE_FLASH_ATTR int                   wifi_get_scan_interval(void);
ICACHE_FLASH_ATTR void                  wifi_set_scan_interval(int interval);
ICACHE_FLASH_ATTR char                  wifi_get_scan_threshold(void);
ICACHE_FLASH_ATTR void                  wifi_set_scan_threshold(char threshold);

ICACHE_FLASH_ATTR char                * wifi_get_ssid(void);
ICACHE_FLASH_ATTR uint8               * wifi_get_bssid(void);
ICACHE_FLASH_ATTR char                * wifi_get_psk(void);
ICACHE_FLASH_ATTR void                  wifi_set_ssid_psk(char *ssid, uint8 *bssid, char *psk);

ICACHE_FLASH_ATTR ip_addr_t           * wifi_get_ip(void);
ICACHE_FLASH_ATTR uint8                 wifi_get_netmask(void);
ICACHE_FLASH_ATTR ip_addr_t           * wifi_get_gw(void);
ICACHE_FLASH_ATTR ip_addr_t           * wifi_get_dns(void);

ICACHE_FLASH_ATTR void                  wifi_set_ip(ip_addr_t *ip);
ICACHE_FLASH_ATTR void                  wifi_set_netmask(uint8 netmask);
ICACHE_FLASH_ATTR void                  wifi_set_gw(ip_addr_t *gw);
ICACHE_FLASH_ATTR void                  wifi_set_dns(ip_addr_t *dns);

ICACHE_FLASH_ATTR void                  wifi_set_station_mode(wifi_connect_callback_t callback, char *hostname);
ICACHE_FLASH_ATTR void                  wifi_connect(uint8 *bssid);
ICACHE_FLASH_ATTR void                  wifi_set_ap_mode(char *hostname);
ICACHE_FLASH_ATTR bool                  wifi_is_connected(void);

ICACHE_FLASH_ATTR bool                  wifi_scan(wifi_scan_callback_t callback);
ICACHE_FLASH_ATTR void                  wifi_auto_scan(void);


#endif /* _ESPGOODIES_WIFI_H */
