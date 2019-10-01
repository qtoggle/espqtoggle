
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


void ping_wdt_init(void) {
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

void ping_wdt_stop(void) {
    if (!ping_interval) {
        return;
    }

    DEBUG_PINGWDT("stopping");

    ping_interval = 0;
    os_timer_disarm(&ping_timer);
}

int ping_wdt_get_interval(void) {
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
