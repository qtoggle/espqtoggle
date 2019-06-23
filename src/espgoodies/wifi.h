
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


    /* IP address */
#define WIFI_AP_IP1                     192
#define WIFI_AP_IP2                     168
#define WIFI_AP_IP3                     5
#define WIFI_AP_IP4                     1

    /* netmask */
#define WIFI_AP_NM1                     255
#define WIFI_AP_NM2                     255
#define WIFI_AP_NM3                     255
#define WIFI_AP_NM4                     0

    /* start DHCP IP address */
#define WIFI_AP_SI1                     192
#define WIFI_AP_SI2                     168
#define WIFI_AP_SI3                     5
#define WIFI_AP_SI4                     100

    /* end DHCP IP address */
#define WIFI_AP_EI1                     192
#define WIFI_AP_EI2                     168
#define WIFI_AP_EI3                     5
#define WIFI_AP_EI4                     150

#define WIFI_AP_PSK                     "espqtoggle"

#define WIFI_SSID_MAX_LEN               32
#define WIFI_BSSID_LEN                  6
#define WIFI_PSK_MAX_LEN                64


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


ICACHE_FLASH_ATTR int                   wifi_get_scan_interval();
ICACHE_FLASH_ATTR void                  wifi_set_scan_interval(int interval);
ICACHE_FLASH_ATTR char                  wifi_get_scan_threshold();
ICACHE_FLASH_ATTR void                  wifi_set_scan_threshold(char threshold);

ICACHE_FLASH_ATTR char                * wifi_get_ssid();
ICACHE_FLASH_ATTR uint8               * wifi_get_bssid();
ICACHE_FLASH_ATTR char                * wifi_get_psk();
ICACHE_FLASH_ATTR void                  wifi_set_ssid_psk(char *ssid, uint8 *bssid, char *psk);

ICACHE_FLASH_ATTR ip_addr_t           * wifi_get_static_ip();
ICACHE_FLASH_ATTR char                  wifi_get_static_netmask();
ICACHE_FLASH_ATTR ip_addr_t           * wifi_get_static_gw();
ICACHE_FLASH_ATTR ip_addr_t           * wifi_get_static_dns();
ICACHE_FLASH_ATTR void                  wifi_set_ip(ip_addr_t *ip, char netmask, ip_addr_t *gw, ip_addr_t *dns);

ICACHE_FLASH_ATTR void                  wifi_set_station_mode(wifi_connect_callback_t callback, char *hostname);
ICACHE_FLASH_ATTR void                  wifi_connect(uint8 *bssid);
ICACHE_FLASH_ATTR void                  wifi_set_ap_mode(char *hostname);
ICACHE_FLASH_ATTR bool                  wifi_is_connected();

ICACHE_FLASH_ATTR bool                  wifi_scan(wifi_scan_callback_t callback);
ICACHE_FLASH_ATTR void                  wifi_auto_scan();


#endif /* _ESPGOODIES_WIFI_H */
