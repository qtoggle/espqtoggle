
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

#include "espgoodies/common.h"
#include "espgoodies/ota.h"
#include "espgoodies/sleep.h"
#include "espgoodies/system.h"
#include "espgoodies/wifi.h"


#define TEMPORARY_CONNECT_INTERVAL 10000 /* How often to attempt to connect, in temporary station mode */


static bool                      station_connected = FALSE;
static bool                      scanning = FALSE;
static wifi_scan_callback_t      scan_callback = NULL;
static wifi_connect_callback_t   station_connect_callback = NULL;
static wifi_ap_client_callback_t ap_client_callback = NULL;
static os_timer_t                temporary_connect_timer;

static ip_addr_t                 manual_ip_address = {0};
static uint8                     manual_netmask = 0;
static ip_addr_t                 manual_gateway = {0};
static ip_addr_t                 manual_dns = {0};

static uint8                     current_bssid[WIFI_BSSID_LEN];
static struct station_config     cached_station_config;
static bool                      cached_station_config_changed = FALSE;
static bool                      cached_station_config_read = FALSE;
static struct ip_info            cached_ip_info;

static bool                      ap_enabled = FALSE;
static bool                      station_enabled = FALSE;
static bool                      temporary_station_enabled = FALSE;
static bool                      temporary_connect_timer_armed = FALSE;

static os_timer_t                auto_scan_timer;
static uint32                    auto_scan_interval = 0;
static int8                      min_rssi_threshold = 0;
static int8                      better_rssi_threshold = 0;
static uint8                     better_bssid[WIFI_BSSID_LEN];
static uint8                     better_counter = 0;
static uint8                     better_count = 0;


static void ICACHE_FLASH_ATTR ensure_station_config_read(void);
static void ICACHE_FLASH_ATTR on_wifi_event(System_Event_t *evt);
static void ICACHE_FLASH_ATTR on_wifi_scan_done(void *arg, STATUS status);
static void ICACHE_FLASH_ATTR on_temporary_connect(void *arg);
static void ICACHE_FLASH_ATTR on_auto_scan(void *arg);
static int  ICACHE_FLASH_ATTR compare_wifi_rssi(const void *a, const void *b);


char *wifi_get_ssid(void) {
    ensure_station_config_read();

    if (cached_station_config.ssid[0]) {
        return (char *) cached_station_config.ssid;
    }
    else {
        return NULL;
    }
}

uint8 *wifi_get_bssid(void) {
    ensure_station_config_read();

    if (cached_station_config.bssid[0] && cached_station_config.bssid_set) {
        return cached_station_config.bssid;
    }
    else {
        return NULL;
    }
}

char *wifi_get_psk(void) {
    ensure_station_config_read();

    if (cached_station_config.password[0]) {
        return (char *) cached_station_config.password;
    }
    else {
        return NULL;
    }
}

uint8 *wifi_get_bssid_current() {
    struct station_config config;
    wifi_station_get_config(&config);
    memcpy(current_bssid, config.bssid, WIFI_BSSID_LEN);

    return current_bssid;
}

void wifi_set_ssid(char *ssid) {
    ensure_station_config_read();

    memset(cached_station_config.ssid, 0, WIFI_SSID_MAX_LEN);
    if (ssid) {
        strncpy((char *) cached_station_config.ssid, ssid, WIFI_SSID_MAX_LEN);
    }

    if (ssid && ssid[0]) {
        DEBUG_WIFI("SSID set to \"%s\"", ssid);
    }
    else {
        DEBUG_WIFI("SSID unset");
    }

    cached_station_config_changed = TRUE;
}

void wifi_set_psk(char *psk) {
    ensure_station_config_read();

    memset(cached_station_config.password, 0, WIFI_PSK_MAX_LEN);
    if (psk) {
        strncpy((char *) cached_station_config.password, psk, WIFI_PSK_MAX_LEN);
    }

    if (psk && psk[0]) {
        DEBUG_WIFI("PSK set");
    }
    else {
        DEBUG_WIFI("PSK unset");
    }

    cached_station_config_changed = TRUE;
}

void wifi_set_bssid(uint8 *bssid) {
    ensure_station_config_read();

    if (bssid) {
        memcpy(cached_station_config.bssid, bssid, WIFI_BSSID_LEN);
    }
    else {
        memset(cached_station_config.bssid, 0, WIFI_BSSID_LEN);
    }
    cached_station_config.bssid_set = (bssid != NULL);

    if (bssid) {
        DEBUG_WIFI("BSSID set to " WIFI_BSSID_FMT, WIFI_BSSID2STR(bssid));
    }
    else {
        DEBUG_WIFI("BSSID unset");
    }

    cached_station_config_changed = TRUE;
}

int wifi_bssid_cmp(uint8 *bssid1, uint8 *bssid2) {
    /* Ignore the first byte as it may actually change */
    return memcmp(bssid1 + 1, bssid2 + 1, WIFI_BSSID_LEN - 1);
}

void wifi_save_config(void) {
    if (cached_station_config_changed) {
        if (!station_enabled) {
            DEBUG_WIFI("attempt to save configuration while station disabled");
        }
        else {
            DEBUG_WIFI("saving configuration");
        }

        /* system_restore() is needed here as without it, wifi_station_set_config() appears to be messing out the Wi-Fi
         * configuration stored in flash */
        system_restore();

        if (!wifi_station_set_config(&cached_station_config)) {
            DEBUG_WIFI("wifi_station_set_config() failed");
        }

        cached_station_config_changed = FALSE;
        cached_station_config_read = TRUE;
    }
}

ip_addr_t wifi_get_ip_address(void) {
    return manual_ip_address;
}

uint8 wifi_get_netmask(void) {
    return manual_netmask;
}

ip_addr_t wifi_get_gateway(void) {
    return manual_gateway;
}

ip_addr_t wifi_get_dns(void) {
    return manual_dns;
}

ip_addr_t wifi_get_ip_address_current(void) {
    wifi_get_ip_info(STATION_IF, &cached_ip_info);

    return cached_ip_info.ip;
}

uint8 wifi_get_netmask_current(void) {
    wifi_get_ip_info(STATION_IF, &cached_ip_info);

    uint8 netmask = 0;
    uint32 netmask_addr = cached_ip_info.netmask.addr;
    while (netmask_addr) {
        netmask++;
        netmask_addr >>= 1;
    }

    return netmask;
}

ip_addr_t wifi_get_gateway_current(void) {
    wifi_get_ip_info(STATION_IF, &cached_ip_info);

    return cached_ip_info.gw;
}

ip_addr_t wifi_get_dns_current(void) {
    return espconn_dns_getserver(0);
}

void wifi_set_ip_address(ip_addr_t ip_address) {
    if (ip_address.addr) {  /* Manual */
        DEBUG_WIFI("IP address: using manual: " WIFI_IP_FMT, IP2STR(&ip_address.addr));
        memcpy(&manual_ip_address, &ip_address, sizeof(ip_addr_t));
    }
    else {  /* DHCP */
        DEBUG_WIFI("IP address: using DHCP");
        manual_ip_address.addr = 0;
    }
}

void wifi_set_netmask(uint8 netmask) {
    if (netmask) {  /* Manual */
        DEBUG_WIFI("netmask: using manual: %d", netmask);
        manual_netmask = netmask;
    }
    else {  /* DHCP */
        DEBUG_WIFI("netmask: using DHCP");
        manual_netmask = 0;
    }
}

void wifi_set_gateway(ip_addr_t gateway) {
    if (gateway.addr) {  /* Manual */
        DEBUG_WIFI("gateway: using manual: " WIFI_IP_FMT, IP2STR(&gateway.addr));
        memcpy(&manual_gateway, &gateway, sizeof(ip_addr_t));
    }
    else {  /* DHCP */
        DEBUG_WIFI("gateway: using DHCP");
        manual_gateway.addr = 0;
    }
}

void wifi_set_dns(ip_addr_t dns) {
    if (dns.addr) {  /* Manual */
        DEBUG_WIFI("DNS: using manual: " WIFI_IP_FMT, IP2STR(&dns.addr));
        memcpy(&manual_dns, &dns, sizeof(ip_addr_t));
    }
    else {  /* DHCP */
        DEBUG_WIFI("DNS: using DHCP");
        manual_dns.addr = 0;
    }
}

void wifi_station_enable(
    char *hostname,
    wifi_connect_callback_t callback,
    uint32 _auto_scan_interval,
    int8 _min_rssi_threshold,
    int8 _better_rssi_threshold,
    uint8 _better_count
) {
    if (station_enabled) {
        DEBUG_WIFI("station already enabled");
        return;
    }

    if (temporary_station_enabled) {
        wifi_station_temporary_disable();
    }

    DEBUG_WIFI("enabling station");
    station_enabled = TRUE;

    if (!wifi_set_opmode_current(ap_enabled ? STATIONAP_MODE : STATION_MODE)) {
        DEBUG_WIFI("wifi_set_opmode_current() failed");
    }

    /* Automatically reconnect, but only when AP disabled */
    if (!wifi_station_set_reconnect_policy(!ap_enabled)) {
        DEBUG_WIFI("wifi_station_set_reconnect_policy() failed");
    }
    if (!wifi_station_get_auto_connect() && !wifi_station_set_auto_connect(TRUE)) {
        DEBUG_WIFI("wifi_station_set_auto_connect() failed");
    }
    DEBUG_WIFI("setting hostname to \"%s\"", hostname);
    if (!wifi_station_set_hostname(hostname)) {
        DEBUG_WIFI("wifi_station_set_hostname() failed");
    }

    ensure_station_config_read();
    if (cached_station_config.ssid[0]) {
        if (cached_station_config.bssid_set) {
            DEBUG_WIFI(
                "connecting to SSID=\"%s\", BSSID=" WIFI_BSSID_FMT,
                (char *) cached_station_config.ssid,
                WIFI_BSSID2STR(cached_station_config.bssid)
            );
        }
        else {
            DEBUG_WIFI("connecting to SSID=\"%s\", no particular BSSID", (char *) cached_station_config.ssid);
        }

        if (wifi_station_get_connect_status() == STATION_IDLE && !wifi_station_connect()) {
            DEBUG_WIFI("wifi_station_connect() failed");
        }
    }
    else {
        DEBUG_WIFI("no configured SSID");
    }

    station_connect_callback = callback;

    /* Set IP configuration */
    if (manual_ip_address.addr &&
        manual_netmask &&
        manual_gateway.addr &&
        manual_dns.addr) {  /* Manual IP configuration */

        if (wifi_station_dhcpc_status() == DHCP_STARTED) {
            DEBUG_WIFI("stopping DHCP client");
            wifi_station_dhcpc_stop();
        }

        struct ip_info info;
        info.ip = manual_ip_address;
        info.gw = manual_gateway;

        int m = manual_netmask;
        info.netmask.addr = 1;
        while (--m) {
            info.netmask.addr = (info.netmask.addr << 1) + 1;
        }

        if (!wifi_set_ip_info(STATION_IF, &info)) {
            DEBUG_WIFI("wifi_set_ip_info() failed");
        }

        espconn_dns_setserver(0, &manual_dns);
    }
    else { /* DHCP */
        if (wifi_station_dhcpc_status() == DHCP_STOPPED) {
            DEBUG_WIFI("starting DHCP client");
            wifi_station_dhcpc_start();
        }
    }

    auto_scan_interval = _auto_scan_interval;
    min_rssi_threshold = _min_rssi_threshold;
    better_rssi_threshold = _better_rssi_threshold;
    better_count = _better_count;

    if (auto_scan_interval) {
        os_timer_disarm(&auto_scan_timer);
        os_timer_setfn(&auto_scan_timer, on_auto_scan, NULL);
        os_timer_arm(&auto_scan_timer, auto_scan_interval * 1000, /* repeat = */ FALSE);
    }
}

void wifi_station_disable(void) {
    if (!station_enabled) {
        DEBUG_WIFI("station already disabled");
        return;
    }

    DEBUG_WIFI("disabling station");
    station_enabled = FALSE;
    station_connect_callback = NULL;

    if (!wifi_station_disconnect()) {
        DEBUG_WIFI("wifi_station_disconnect() failed");
    }

    if (!wifi_set_opmode_current(ap_enabled ? SOFTAP_MODE : NULL_MODE)) {
        DEBUG_WIFI("wifi_set_opmode_current() failed");
    }
}

void wifi_station_temporary_enable(
    char *ssid,
    char *psk,
    uint8 *bssid,
    char *hostname,
    wifi_connect_callback_t callback
) {
    if (station_enabled) {
        DEBUG_WIFI("station already enabled");
        return;
    }

    DEBUG_WIFI("enabling temporary station");
    temporary_station_enabled = TRUE;

    if (!wifi_set_opmode_current(ap_enabled ? STATIONAP_MODE : STATION_MODE)) {
        DEBUG_WIFI("wifi_set_opmode_current() failed");
    }

    /* Automatically reconnect */
    if (!wifi_station_set_reconnect_policy(FALSE)) {
        DEBUG_WIFI("wifi_station_set_reconnect_policy() failed");
    }
    if (wifi_station_get_auto_connect() && !wifi_station_set_auto_connect(FALSE)) {
        DEBUG_WIFI("wifi_station_set_auto_connect() failed");
    }
    DEBUG_WIFI("setting hostname to \"%s\"", hostname);
    if (!wifi_station_set_hostname(hostname)) {
        DEBUG_WIFI("wifi_station_set_hostname() failed");
    }

    if (bssid) {
        DEBUG_WIFI("connecting to SSID=\"%s\", BSSID=" WIFI_BSSID_FMT, (char *) ssid, WIFI_BSSID2STR(bssid));
    }
    else {
        DEBUG_WIFI("connecting to SSID=\"%s\", no particular BSSID", (char *) ssid);
    }

    struct station_config config;
    memset(&config, 0, sizeof(struct station_config));
    strncpy((char *) config.ssid, ssid, WIFI_SSID_MAX_LEN);
    if (psk) {
        strncpy((char *) config.password, psk, WIFI_PSK_MAX_LEN);
    }
    if (bssid) {
        memcpy((char *) config.bssid, bssid, WIFI_BSSID_LEN);
        config.bssid_set = 1;
    }

    if (!wifi_station_set_config_current(&config)) {
        DEBUG_WIFI("wifi_station_set_config_current() failed");
    }

    if (!wifi_station_connect()) {
        DEBUG_WIFI("wifi_station_connect() failed");
    }

    os_timer_setfn(&temporary_connect_timer, on_temporary_connect, NULL);
    os_timer_arm(&temporary_connect_timer, TEMPORARY_CONNECT_INTERVAL, /* repeat = */ TRUE);
    temporary_connect_timer_armed = TRUE;

    station_connect_callback = callback;

    /* Always use DHCP for temporary station mode */
    if (wifi_station_dhcpc_status() == DHCP_STOPPED) {
        DEBUG_WIFI("starting DHCP client");
        wifi_station_dhcpc_start();
    }
}

void wifi_station_temporary_disable(void) {
    if (!temporary_station_enabled) {
        DEBUG_WIFI("temporary station already disabled");
        return;
    }

    DEBUG_WIFI("disabling temporary station");
    temporary_station_enabled = FALSE;
    station_connect_callback = NULL;

    if (!wifi_station_disconnect()) {
        DEBUG_WIFI("wifi_station_disconnect() failed");
    }

    if (!wifi_set_opmode_current(ap_enabled ? SOFTAP_MODE : NULL_MODE)) {
        DEBUG_WIFI("wifi_set_opmode_current() failed");
    }

    os_timer_disarm(&temporary_connect_timer);
    temporary_connect_timer_armed = FALSE;
}

bool wifi_station_is_connected(void) {
    return station_connected;
}


void wifi_ap_enable(char *ssid, char *psk, wifi_ap_client_callback_t callback) {
    if (ap_enabled) {
        DEBUG_WIFI("AP already enabled");
        return;
    }

    DEBUG_WIFI("enabling AP");
    ap_enabled = TRUE;

    ap_client_callback = callback;

    if (!wifi_set_opmode_current((station_enabled || temporary_station_enabled) ? STATIONAP_MODE : SOFTAP_MODE)) {
        DEBUG_WIFI("wifi_set_opmode_current() failed");
    }

    /* Disable station automatic reconnection */
    if (station_enabled || temporary_station_enabled) {
        if (!wifi_station_set_reconnect_policy(FALSE)) {
            DEBUG_WIFI("wifi_station_set_reconnect_policy() failed");
        }
    }

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

    struct softap_config config;

    memset(&config, 0, sizeof(struct softap_config));
    strcpy((char *) config.ssid, ssid);
    strcpy((char *) config.password, (psk && psk[0]) ? psk : "");
    config.ssid_len = strlen(ssid);
    config.authmode = (psk && psk[0]) ? AUTH_WPA2_PSK : AUTH_OPEN;
    config.channel = 0;
    config.ssid_hidden = FALSE;
    config.max_connection = 4;
    config.beacon_interval = 100;

    if (!wifi_softap_set_config_current(&config)) {
        DEBUG_WIFI("wifi_softap_set_config_current() failed");
    }

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

void wifi_ap_disable(void) {
    if (!ap_enabled) {
        DEBUG_WIFI("AP already disabled");
        return;
    }

    DEBUG_WIFI("disabling AP");
    ap_enabled = FALSE;

    if (!wifi_set_opmode_current((station_enabled || temporary_station_enabled) ? STATION_MODE : NULL_MODE)) {
        DEBUG_WIFI("wifi_set_opmode_current() failed");
    }

    if (station_enabled) {
        /* Enable station automatic reconnection */
        if (!wifi_station_set_reconnect_policy(TRUE)) {
            DEBUG_WIFI("wifi_station_set_reconnect_policy() failed");
        }
    }
}

bool wifi_scan(wifi_scan_callback_t callback) {
    if (!station_enabled && !temporary_station_enabled) {
        DEBUG_WIFI("cannot scan when station disabled");
        return FALSE;
    }

    if (scanning) {
        DEBUG_WIFI("attempt to scan while already scanning");
        return FALSE; /* Already scanning */
    }

#ifdef _OTA
    if (ota_busy()) {
        DEBUG_WIFI("attempt to scan while OTA busy");
        return FALSE; /* OTA started */
    }
#endif

    DEBUG_WIFI("starting AP scan");

    scanning = TRUE;

    if (scan_callback) {
        DEBUG_WIFI("scan callback already set");
    }
    scan_callback = callback;

    struct scan_config conf;
    memset(&conf, 0, sizeof(struct scan_config));

    /* Start scanning for APs */
    wifi_station_scan(&conf, on_wifi_scan_done);

    return TRUE;
}

void wifi_init(void) {
    DEBUG_WIFI("initializing");

    /* Start with both AP and station modes disabled */
    if (!wifi_set_opmode_current(NULL_MODE)) {
        DEBUG_WIFI("wifi_set_opmode_current() failed");
    }

    /* Disable sleep/power saving mode, as it is known to cause Wi-Fi connectivity issues */
    if (!wifi_set_sleep_type(NONE_SLEEP_T)) {
        DEBUG_WIFI("wifi_set_sleep_type() failed");
    }

    wifi_set_event_handler_cb(on_wifi_event);
}

void wifi_reset(void) {
    DEBUG_WIFI("resetting to factory defaults");
    system_restore();
}


void ensure_station_config_read(void) {
    if (!cached_station_config_read) {
        DEBUG_WIFI("loading configuration");

        wifi_station_get_config_default(&cached_station_config);
        cached_station_config_read = TRUE;
        cached_station_config_changed = FALSE;

        /* Check if SSID has fields starting with many 0xFF - that normally indicates flash erased */
        if (!memcmp(cached_station_config.ssid, "\xFF\xFF\xFF\xFF", 4)) {
            cached_station_config.ssid[0] = 0;
        }
    }
}

void on_wifi_event(System_Event_t *evt) {
    char *event_name = NULL;

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
            /* This event is received too often and just floods the serial log */
            /* event_name = "softap_probe_req_recved"; */
            break;

        case EVENT_OPMODE_CHANGED:
            event_name = "opmode_changed";
            break;

        case EVENT_SOFTAPMODE_DISTRIBUTE_STA_IP:
            event_name = "softap_distribute_sta_ip";
            break;
    }

    if (event_name) {
        DEBUG_WIFI("got \"%s\" event", event_name);
    }

     switch (evt->event) {
         case EVENT_STAMODE_DISCONNECTED:
             DEBUG_WIFI(
                 "disconnected from SSID \"%s\", BSSID " WIFI_BSSID_FMT ", reason %d",
                 evt->event_info.disconnected.ssid,
                 WIFI_BSSID2STR(evt->event_info.disconnected.bssid),
                 evt->event_info.disconnected.reason
             );

             if (station_connect_callback && station_connected) {
                 station_connect_callback(FALSE);
             }

             station_connected = FALSE;

             /* Reconnect to temporary station */
             if (temporary_station_enabled) {

                 /* Ensure reconnect timer is armed */
                 if (!temporary_connect_timer_armed) {
                     os_timer_arm(&temporary_connect_timer, TEMPORARY_CONNECT_INTERVAL, /* repeat = */ TRUE);
                     temporary_connect_timer_armed = TRUE;
                 }

                 /* If auth expired, try to reconnect right away */
                 if (evt->event_info.disconnected.reason == REASON_AUTH_EXPIRE) {
                     on_temporary_connect(NULL);
                 }
             }

             break;

         case EVENT_STAMODE_GOT_IP:
             DEBUG_WIFI("connected and ready at " WIFI_IP_FMT, IP2STR(&evt->event_info.got_ip.ip));

             if (station_connect_callback && !station_connected) {
                 station_connect_callback(TRUE);
             }

             station_connected = TRUE;

             /* In case temporary connection is enabled, stop attempting to connect as soon as connected */
             os_timer_disarm(&temporary_connect_timer);
             temporary_connect_timer_armed = FALSE;

              break;

         case EVENT_SOFTAPMODE_STACONNECTED:
             DEBUG_WIFI("AP client with MAC "WIFI_MAC_FMT " connected", MAC2STR(evt->event_info.distribute_sta_ip.mac));

             /* In case temporary connection is enabled, stop attempting to connect as soon as the first AP client
              * connects. Continuously attempting to connect breaks the communication with the new AP client, probably
              * due to channel hopping during scanning. However, if we're already connected, don't disconnect. */
             os_timer_disarm(&temporary_connect_timer);
             temporary_connect_timer_armed = FALSE;

             break;

         case EVENT_SOFTAPMODE_DISTRIBUTE_STA_IP:
             DEBUG_WIFI(
                 "AP client with MAC " WIFI_MAC_FMT " was given IP " WIFI_IP_FMT,
                 MAC2STR(evt->event_info.distribute_sta_ip.mac),
                 IP2STR(&evt->event_info.distribute_sta_ip.ip.addr)
             );

             if (ap_client_callback) {
                 ap_client_callback(TRUE, evt->event_info.distribute_sta_ip.ip, evt->event_info.distribute_sta_ip.mac);
             }

             break;

         case EVENT_SOFTAPMODE_STADISCONNECTED: {
             DEBUG_WIFI(
                 "AP client with MAC " WIFI_MAC_FMT " disconnected",
                 MAC2STR(evt->event_info.distribute_sta_ip.mac)
             );

             if (ap_client_callback) {
                 ap_client_callback(FALSE, (ip_addr_t){0}, evt->event_info.distribute_sta_ip.mac);
             }

             break;
         }
     }
}

void on_wifi_scan_done(void *arg, STATUS status) {
    wifi_scan_callback_t callback = scan_callback;

    scanning = FALSE;
    scan_callback = NULL;

    if (status != OK) {
        DEBUG_WIFI("scan failed");
        if (callback) {
            callback(NULL, 0);
        }

        return;
    }

    DEBUG_WIFI("got scan results");

    wifi_scan_result_t *results = NULL;
    int len = 0;
    struct bss_info *result = (struct bss_info *) arg;

    while (result) {
        result->ssid[result->ssid_len] = 0;
        DEBUG_WIFI(
            "found SSID=\"%s\", channel=%d, RSSI=%d, BSSID=" WIFI_BSSID_FMT,
            result->ssid,
            result->channel,
            result->rssi,
            WIFI_BSSID2STR(result->bssid)
        );

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

    if (callback) {
        callback(results, len);
    }

    /* Jump to a better AP, if:
     *  * we're in pure station mode
     *  * we're currently connected
     *  * we don't have a particular BSSID specified
     *  * auto scan is enabled
     *  * we actually found a considerably better AP
     *  * it's not the same AP to which we're currently connected
     *  * the better AP RSSI is consistently higher than the current one */

    wifi_scan_result_t *best_result = len > 0 ? results + 0 : NULL;
    int32 rssi = wifi_station_get_rssi();

    if (!callback && /* automatic AP scan */
        station_enabled &&
        !ap_enabled &&
        station_connected &&
        wifi_get_bssid() == NULL && /* we don't have a particular BSSID specified */
        auto_scan_interval && /* automatic AP scan mechanism enabled */
        rssi < min_rssi_threshold &&
        best_result != NULL &&
        best_result->rssi - rssi > better_rssi_threshold &&
        !strncmp(best_result->ssid, wifi_get_ssid(), 32) &&
        wifi_bssid_cmp(wifi_get_bssid_current(), best_result->bssid) && /* ignore currently connected AP */
        (!better_bssid[0] || !wifi_bssid_cmp(better_bssid, best_result->bssid)) /* same better AP as the last time */
    ) {
        better_counter++;
        memcpy(better_bssid, best_result->bssid, WIFI_BSSID_LEN);

        DEBUG_WIFI(
            "we have a better AP with BSSID " WIFI_BSSID_FMT " and RSSI %d > %d (counter = %d)",
            WIFI_BSSID2STR(best_result->bssid),
            best_result->rssi,
            rssi,
            better_counter
        );
    }
    else {
        better_counter = 0;
        memset(better_bssid, 0, WIFI_BSSID_LEN);
    }

    if (better_counter >= better_count) {
        DEBUG_WIFI("attempting to reconnect to better AP");

        better_counter = 0;
        memset(better_bssid, 0, WIFI_BSSID_LEN);

        if (!wifi_station_disconnect()) {
            DEBUG_WIFI("wifi_station_disconnect() failed");
        }
        if (!wifi_station_connect()) {
            DEBUG_WIFI("wifi_station_connect() failed");
        }
    }
}

void on_temporary_connect(void *arg) {
    DEBUG_WIFI("temporary connection attempt");
    if (!wifi_station_connect()) {
        DEBUG_WIFI("wifi_station_connect() failed");
    }
}

void on_auto_scan(void *arg) {
    if (!station_enabled || !auto_scan_interval) {
        return; /* Auto-scanning is disabled as soon as the device leaves station mode */
    }

    /* Only scan in pure station mode, but keep scheduling the next call */
    int32 rssi = wifi_station_get_rssi();
    if (!ap_enabled && rssi < min_rssi_threshold) {
        DEBUG_WIFI("scanning for a better AP (RSSI is %d)", rssi);
        wifi_scan(/* callback = */ NULL);
    }
    else {
        DEBUG_WIFI("skipping auto-scan");
    }

    /* Schedule next call */
    os_timer_arm(&auto_scan_timer, auto_scan_interval * 1000, /* repeat = */ FALSE);
}

int compare_wifi_rssi(const void *a, const void *b) {
    int x = ((wifi_scan_result_t *) a)->rssi;
    int y = ((wifi_scan_result_t *) b)->rssi;

    return y - x;
}
