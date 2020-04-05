
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
#include <osapi.h>
#include <ip_addr.h>
#include <espconn.h>
#include <user_interface.h>
#include <mem.h>

#include "common.h"
#include "system.h"
#include "wifi.h"

#ifdef _SLEEP
#include "sleep.h"
#endif

#ifdef _OTA
#include "ota.h"
#endif


#define WIFI_WATCHDOG_INTERVAL      10  /* seconds */
#define WIFI_WATCHDOG_MAX_COUNT     3   /* disconnected times, in a row, that trigger a reset */


/* we have to maintain a separate connected status, as wifi_station_get_connect_status() appears to return  wrong values
 * after disconnect */
static bool                         wifi_connected = FALSE;
static bool                         wifi_scanning = FALSE;
static wifi_scan_callback_t         wifi_scan_callback = NULL;
static wifi_connect_callback_t      wifi_connect_callback = NULL;
static os_timer_t                   wifi_watchdog_timer;
static int                          wifi_watchdog_counter = 0;

static char                         wifi_ssid[WIFI_SSID_MAX_LEN] = {0};
static uint8                        wifi_bssid[WIFI_BSSID_LEN] = {0};
static char                         wifi_psk[WIFI_PSK_MAX_LEN] = {0};
static ip_addr_t                    wifi_static_ip_address = {0};
static uint8                        wifi_static_netmask = 0;
static ip_addr_t                    wifi_static_gateway = {0};
static ip_addr_t                    wifi_static_dns = {0};
static struct ip_info               wifi_current_ip_info;


ICACHE_FLASH_ATTR static void       on_wifi_event(System_Event_t *evt);
ICACHE_FLASH_ATTR static void       on_wifi_scan_done(void *arg, STATUS status);
ICACHE_FLASH_ATTR static void       on_wifi_watchdog(void *arg);
ICACHE_FLASH_ATTR static int        compare_wifi_rssi(const void *a, const void *b);


char *wifi_get_ssid(void) {
    return wifi_ssid;
}

uint8 *wifi_get_bssid(void) {
    return wifi_bssid;
}

char *wifi_get_psk(void) {
    return wifi_psk;
}

void wifi_get_bssid_current(uint8 *bssid) {
    struct station_config conf;
    wifi_station_get_config(&conf);

    memcpy(bssid, conf.bssid, WIFI_BSSID_LEN);
}

void wifi_set_ssid(char *ssid) {
    strncpy(wifi_ssid, ssid, WIFI_SSID_MAX_LEN);
    wifi_ssid[WIFI_SSID_MAX_LEN - 1] = 0;
    if (wifi_ssid[0]) {
        DEBUG_WIFI("ssid set to %s", ssid);
    }
    else {
        DEBUG_WIFI("ssid not set");
    }
}

void wifi_set_psk(char *psk) {
    strncpy(wifi_psk, psk, WIFI_PSK_MAX_LEN);
    wifi_psk[WIFI_PSK_MAX_LEN - 1] = 0;
    if (psk[0]) {
        DEBUG_WIFI("psk set");
    }
    else {
        DEBUG_WIFI("psk not set");
    }
}

void wifi_set_bssid(uint8 *bssid) {
    memcpy(wifi_bssid, bssid, WIFI_BSSID_LEN);
    if (bssid[0]) {
        DEBUG_WIFI("bssid set to " BSSID_FMT, BSSID2STR(bssid));
    }
    else {
        DEBUG_WIFI("bssid not set");
    }
}

ip_addr_t wifi_get_ip_address(void) {
    return wifi_static_ip_address;
}

uint8 wifi_get_netmask(void) {
    return wifi_static_netmask;
}

ip_addr_t wifi_get_gateway(void) {
    return wifi_static_gateway;
}

ip_addr_t wifi_get_dns(void) {
    return wifi_static_dns;
}

ip_addr_t wifi_get_ip_address_current(void) {
    wifi_get_ip_info(STATION_IF, &wifi_current_ip_info);

    return wifi_current_ip_info.ip;
}

uint8 wifi_get_netmask_current(void) {
    wifi_get_ip_info(STATION_IF, &wifi_current_ip_info);

    uint8 netmask = 0;
    uint32 netmask_addr = wifi_current_ip_info.netmask.addr;
    while (netmask_addr) {
        netmask++;
        netmask_addr >>= 1;
    }

    return netmask;
}

ip_addr_t wifi_get_gateway_current(void) {
    wifi_get_ip_info(STATION_IF, &wifi_current_ip_info);

    return wifi_current_ip_info.gw;
}

ip_addr_t wifi_get_dns_current(void) {
    return espconn_dns_getserver(0);
}

void wifi_set_ip_address(ip_addr_t ip_address) {
    if (ip_address.addr) {  /* manual */
        DEBUG_WIFI("IP address: using manual: " IPSTR, IP2STR(&ip_address.addr));
        memcpy(&wifi_static_ip_address, &ip_address, sizeof(ip_addr_t));
    }
    else {  /* DHCP */
        DEBUG_WIFI("IP address: using DHCP");
        wifi_static_ip_address.addr = 0;
    }
}

void wifi_set_netmask(uint8 netmask) {
    if (netmask) {  /* manual */
        DEBUG_WIFI("netmask: using manual: %d", netmask);
        wifi_static_netmask = netmask;
    }
    else {  /* DHCP */
        DEBUG_WIFI("netmask: using DHCP");
        wifi_static_netmask = 0;
    }
}

void wifi_set_gateway(ip_addr_t gateway) {
    if (gateway.addr) {  /* manual */
        DEBUG_WIFI("gateway: using manual: " IPSTR, IP2STR(&gateway.addr));
        memcpy(&wifi_static_gateway, &gateway, sizeof(ip_addr_t));
    }
    else {  /* DHCP */
        DEBUG_WIFI("gateway: using DHCP");
        wifi_static_gateway.addr = 0;
    }
}

void wifi_set_dns(ip_addr_t dns) {
    if (dns.addr) {  /* manual */
        DEBUG_WIFI("DNS: using manual: " IPSTR, IP2STR(&dns.addr));
        memcpy(&wifi_static_dns, &dns, sizeof(ip_addr_t));
    }
    else {  /* DHCP */
        DEBUG_WIFI("DNS: using DHCP");
        wifi_static_dns.addr = 0;
    }
}

void wifi_set_station_mode(wifi_connect_callback_t callback, char *hostname) {
    if (!wifi_set_opmode_current(STATION_MODE)) {
        DEBUG_WIFI("wifi_set_opmode_current() failed");
    }
    if (!wifi_station_set_hostname(hostname)) {
        DEBUG_WIFI("wifi_station_set_hostname() failed");
    }
    if (!wifi_set_sleep_type(NONE_SLEEP_T)) {
        DEBUG_WIFI("wifi_set_sleep_type() failed");
    }
    if (!wifi_station_set_reconnect_policy(FALSE)) {
        DEBUG_WIFI("wifi_station_set_reconnect_policy() failed");
    }
    if (!wifi_station_set_auto_connect(FALSE)) {
        DEBUG_WIFI("wifi_station_set_auto_connect() failed");
    }

    wifi_set_event_handler_cb(on_wifi_event);

    /* initialize watchdog timer */
#ifdef _SLEEP
    if (!sleep_is_short_wake()) {
#endif
        DEBUG_WIFI("initializing watchdog");
        os_timer_disarm(&wifi_watchdog_timer);
        os_timer_setfn(&wifi_watchdog_timer, on_wifi_watchdog, NULL);
        os_timer_arm(&wifi_watchdog_timer, WIFI_WATCHDOG_INTERVAL * 1000, /* repeat = */ TRUE);
#ifdef _SLEEP
    }
#endif

    wifi_connect_callback = callback;
}

void wifi_connect(uint8 *bssid) {
    struct station_config conf;

    memset(&conf, 0, sizeof(struct station_config));

    conf.bssid_set = 1;

    memcpy(conf.ssid, wifi_ssid, WIFI_SSID_MAX_LEN);
    memcpy(conf.bssid, bssid, WIFI_BSSID_LEN);
    strncpy((char *) conf.password, wifi_psk, WIFI_PSK_MAX_LEN);

    DEBUG_WIFI("connecting to ssid/bssid %s/" BSSID_FMT, wifi_ssid, BSSID2STR(bssid));

    /* set IP configuration */
    if (wifi_static_ip_address.addr &&
        wifi_static_netmask &&
        wifi_static_gateway.addr &&
        wifi_static_dns.addr) {  /* manual IP configuration */

        if (wifi_station_dhcpc_status() == DHCP_STARTED) {
            DEBUG_WIFI("stopping DHCP client");
            wifi_station_dhcpc_stop();
        }

        struct ip_info info;
        info.ip = wifi_static_ip_address;
        info.gw = wifi_static_gateway;

        int m = wifi_static_netmask;
        info.netmask.addr = 1;
        while (--m) {
            info.netmask.addr = (info.netmask.addr << 1) + 1;
        }

        if (!wifi_set_ip_info(STATION_IF, &info)) {
            DEBUG_WIFI("wifi_set_ip_info() failed");
        }

        espconn_dns_setserver(0, &wifi_static_dns);
    }
    else {  /* DHCP */
        if (wifi_station_dhcpc_status() == DHCP_STOPPED) {
            DEBUG_WIFI("starting DHCP client");
            wifi_station_dhcpc_start();
        }
    }

    if (!wifi_station_set_config_current(&conf)) {
        DEBUG_WIFI("wifi_station_set_config_current() failed");
    }
    if (!wifi_station_connect()) {
        DEBUG_WIFI("wifi_station_connect() failed");
    }
}

void wifi_set_ap_mode(char *hostname) {
    if (!wifi_set_opmode_current(STATIONAP_MODE)) {
        DEBUG_WIFI("wifi_set_opmode_current() failed");
    }

    os_delay_us(65535);

    if (!wifi_softap_dhcps_stop()) {
        DEBUG_WIFI("wifi_softap_dhcps_stop() failed");
    }

    struct ip_info info;
    IP4_ADDR(&info.ip, WIFI_AP_IP1, WIFI_AP_IP2, WIFI_AP_IP3, WIFI_AP_IP4);
    IP4_ADDR(&info.gw, WIFI_AP_IP1, WIFI_AP_IP2, WIFI_AP_IP3, WIFI_AP_IP4);
    IP4_ADDR(&info.netmask, WIFI_AP_NM1, WIFI_AP_NM2, WIFI_AP_NM3, WIFI_AP_NM4);
    if (!wifi_set_ip_info(SOFTAP_IF, &info)) {
        DEBUG_WIFI("wifi_set_ip_info() failed");
    }

    os_delay_us(65535);

    char ssid[32];
    snprintf(ssid, sizeof(ssid), hostname, system_get_chip_id());

    struct softap_config config;

    memset(&config, 0, sizeof(struct softap_config));
    strcpy((char *) config.ssid, ssid);
    strcpy((char *) config.password, WIFI_AP_PSK ? WIFI_AP_PSK : "");
    config.ssid_len = strlen(ssid);
    config.authmode = WIFI_AP_PSK ? AUTH_WPA2_PSK : AUTH_OPEN;
    config.channel = 0;
    config.ssid_hidden = FALSE;
    config.max_connection = 4;
    config.beacon_interval = 100;

    if (!wifi_softap_set_config_current(&config)) {
        DEBUG_WIFI("wifi_softap_set_config_current() failed");
    }

    os_delay_us(65535);

    struct dhcps_lease lease;
    lease.enable = TRUE;

    IP4_ADDR(&lease.start_ip, WIFI_AP_SI1, WIFI_AP_SI2, WIFI_AP_SI3, WIFI_AP_SI4);
    IP4_ADDR(&lease.end_ip, WIFI_AP_EI1, WIFI_AP_EI2, WIFI_AP_EI3, WIFI_AP_EI4);
    if (!wifi_softap_set_dhcps_lease(&lease)) {
        DEBUG_WIFI("wifi_softap_set_dhcps_lease() failed");
    }

    if (!wifi_softap_dhcps_start()) {
        DEBUG_WIFI("wifi_softap_dhcps_start() failed");
    }

    system_phy_set_max_tpw(82 /* 20.5 dBm * 4 */);
}

bool wifi_is_connected(void) {
    return wifi_connected;
}

bool wifi_scan(wifi_scan_callback_t callback) {
    if (wifi_scanning) {
        DEBUG_WIFI("attempt to scan while already scanning");
        return FALSE; /* already scanning */
    }

#ifdef _OTA
    if (ota_busy()) {
        DEBUG_WIFI("attempt to scan while OTA busy");
        return FALSE; /* ota started */
    }
#endif

    DEBUG_WIFI("starting general ap scan");

    wifi_scanning = TRUE;

    if (wifi_scan_callback) {
        DEBUG_WIFI("scan callback already set");
    }
    wifi_scan_callback = callback;

    struct scan_config conf;
    memset(&conf, 0, sizeof(struct scan_config));

    /* start scanning for APs */
    wifi_station_scan(&conf, on_wifi_scan_done);

    return TRUE;
}

void on_wifi_event(System_Event_t *evt) {
    char *event_name = "unknown";

    switch (evt->event) {
        case EVENT_STAMODE_CONNECTED:
            event_name = "sta_connected";
            break;

        case EVENT_STAMODE_DISCONNECTED:
            event_name = "sta_disconnected";
            break;

        case EVENT_STAMODE_AUTHMODE_CHANGE:
            event_name = "sta_auth_mode_change";
            break;

        case EVENT_STAMODE_GOT_IP:
            event_name = "sta_got_ip";
            break;

        case EVENT_STAMODE_DHCP_TIMEOUT:
            event_name = "sta_dhcp_timeout";
            break;

        case EVENT_SOFTAPMODE_STACONNECTED:
            event_name = "softap_sta_connected";
            break;

        case EVENT_SOFTAPMODE_STADISCONNECTED:
            event_name = "softap_sta_disconnected";
            break;

        case EVENT_SOFTAPMODE_PROBEREQRECVED:
            event_name = "softap_probe_req_recved";
            break;

        case EVENT_OPMODE_CHANGED:
            event_name = "opmode_changed";
            break;

        case EVENT_SOFTAPMODE_DISTRIBUTE_STA_IP:
            event_name = "softap_distribute_sta_ip";
            break;
    }

    DEBUG_WIFI("got \"%s\" event", event_name);

     switch (evt->event) {
         case EVENT_STAMODE_DISCONNECTED:
             wifi_connected = FALSE;

             DEBUG_WIFI("disconnected from ssid %s, bssid " BSSID_FMT ", reason %d",
                        evt->event_info.disconnected.ssid,
                        BSSID2STR(evt->event_info.disconnected.bssid),
                        evt->event_info.disconnected.reason);

             if (wifi_connect_callback) {
                 wifi_connect_callback(FALSE);
             }

             break;

         case EVENT_STAMODE_GOT_IP:
             DEBUG_WIFI("connected and ready at " IPSTR, IP2STR(&evt->event_info.got_ip.ip));

             wifi_connected = TRUE;

             if (wifi_connect_callback) {
                 wifi_connect_callback(TRUE);
             }

             break;
     }
}

void on_wifi_scan_done(void *arg, STATUS status) {
    wifi_scan_callback_t callback = wifi_scan_callback;

    wifi_scanning = FALSE;
    wifi_scan_callback = NULL;

    if (status != OK) {
        DEBUG_WIFI("scan failed");
        if (callback) {
            callback(NULL, 0);
        }

        return;
    }

    wifi_scan_result_t *results = NULL;
    int len = 0;
    struct bss_info *result = (struct bss_info *) arg;

    while (result) {
        result->ssid[result->ssid_len] = 0;
        DEBUG_WIFI("found ssid \"%s\" on channel %d, rssi %d, bssid " BSSID_FMT,
                   result->ssid, result->channel, result->rssi, BSSID2STR(result->bssid));

        results = realloc(results, sizeof(wifi_scan_result_t) * (len + 1));

        memcpy(results[len].bssid, result->bssid, sizeof(result->bssid));
        memcpy(results[len].ssid, result->ssid, result->ssid_len);
        results[len].ssid[result->ssid_len] = 0;
        results[len].channel = result->channel;
        results[len].rssi = result->rssi;
        results[len].auth_mode = result->authmode;

        len++;
        result = result->next.stqe_next;
    }

    qsort(results, len, sizeof(wifi_scan_result_t), compare_wifi_rssi);

    callback(results, len);
}

void on_wifi_watchdog(void *arg) {
    bool connected = wifi_connected && (wifi_station_get_connect_status() == STATION_GOT_IP);
    bool override = FALSE;

#ifdef _OTA
    if (ota_busy()) {
        override = TRUE;
    }
#endif

    if (system_setup_mode_active()) {
        override = TRUE;
    }

    DEBUG_WIFI("watchdog: connected = %d, override = %d", connected, override);
    if (connected || override) {
        wifi_watchdog_counter = 0;
    }
    else {
        if (wifi_watchdog_counter < WIFI_WATCHDOG_MAX_COUNT) {
            wifi_watchdog_counter++;
            DEBUG_WIFI("watchdog: counter = %d", wifi_watchdog_counter);
        }
        else {
            DEBUG_WIFI("watchdog: counter = %d, resetting system", wifi_watchdog_counter);
            system_reset(/* delayed = */ FALSE);
        }
    }
}

int compare_wifi_rssi(const void *a, const void *b) {
    int x = ((wifi_scan_result_t *) a)->rssi;
    int y = ((wifi_scan_result_t *) b)->rssi;

    return y - x;
}
