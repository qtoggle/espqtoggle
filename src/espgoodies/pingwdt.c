
/*
 * Copyright (c) Calin Crisan
 * This file is part of espqToggle.
 *
 * espqToggle is free software: you can redistribute it and/or modify
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

#include <mem.h>
#include <os_type.h>
#include <ping.h>
#include <user_interface.h>

#include "common.h"
#include "system.h"
#include "pingwdt.h"

#ifdef _SLEEP
#include "sleep.h"
#endif

#ifdef _OTA
#include "ota.h"
#endif


static int                  ping_interval;
static int                  ping_err_count;

static os_timer_t           ping_timer;
static int                  ping_err_counter;

static struct ping_option   ping_opt;


ICACHE_FLASH_ATTR void      on_ping(void *arg);
ICACHE_FLASH_ATTR void      on_ping_recv(void *arg, void *pdata);
ICACHE_FLASH_ATTR void      on_ping_sent(void *arg, void *pdata);


void ping_wdt_init() {
    ping_regist_recv(&ping_opt, on_ping_recv);
    ping_regist_sent(&ping_opt, on_ping_sent);
}

void ping_wdt_start(int interval) {
    if (ping_interval) {
        ping_wdt_stop();
    }

#ifdef _SLEEP
    if (sleep_is_short_wake()) {
        DEBUG_PINGWDT("refusing to start in sleep short wake mode");
        return;
    }
#endif

    DEBUG_PINGWDT("starting (interval=%ds)", interval);

    ping_interval = interval;
    ping_err_count = PING_WDT_ERR_COUNT;

    ping_err_counter = 0;

    os_timer_disarm(&ping_timer);
    os_timer_setfn(&ping_timer, on_ping, NULL);
    os_timer_arm(&ping_timer, ping_interval * 1000, /* repeat = */ TRUE);
}

void ping_wdt_stop() {
    if (!ping_interval) {
        return;
    }

    DEBUG_PINGWDT("stopping");

    ping_interval = 0;
    os_timer_disarm(&ping_timer);
}

int ping_wdt_get_interval() {
    return ping_interval;
}

void on_ping(void *arg) {
    struct ip_info info;

    /* ping the gateway */
    wifi_get_ip_info(STATION_IF, &info);

    /* ping timeout is hardcoded to 1000ms, for some reason */
    ping_opt.count = 1;
    ping_opt.coarse_time = 1;
    ping_opt.ip = info.gw.addr;

    if (!ping_opt.ip) {
        /* not connected (yet) */
        return;
    }

    DEBUG_PINGWDT("pinging %d.%d.%d.%d", IP2STR(&ping_opt.ip));
    ping_start(&ping_opt);
}

void on_ping_recv(void *arg, void *pdata) {
    struct ping_resp *ping_resp = pdata;
    bool override = FALSE;

    if (ping_resp->ping_err == -1) {
        DEBUG_PINGWDT("ping timeout (err_count=%d)", ping_err_counter);

#ifdef _OTA
        if (ota_busy()) {
            override = TRUE;
        }
#endif

        if (system_setup_mode_active()) {
            override = TRUE;
        }

        if (override) {
            ping_err_counter = 0;
        }
        else {
            /* increase error counter */
            ping_err_counter++;

            /* have we reached reset error threshold? */
            if (ping_err_counter >= ping_err_count) {
                DEBUG_PINGWDT("%d.%d.%d.%d is not reachable, resetting system", IP2STR(&ping_opt.ip));
                system_reset(/* delayed = */ FALSE);
            }
        }
    }
    else {
        DEBUG_PINGWDT("ping reply from %d.%d.%d.%d (time=%dms, seq=%d)", IP2STR(&ping_opt.ip),
                      ping_resp->resp_time, ping_resp->seqno);

        /* reset error counter */
        ping_err_counter = 0;
    }
}

void on_ping_sent(void *arg, void *pdata) {
}
