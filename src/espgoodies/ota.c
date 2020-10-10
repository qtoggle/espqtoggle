
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

#include <user_interface.h>
#include <mem.h>
#include <upgrade.h>
#include <ctype.h>

#include "espgoodies/common.h"
#include "espgoodies/httpclient.h"
#include "espgoodies/system.h"
#include "espgoodies/tcpserver.h"
#include "espgoodies/utils.h"
#include "espgoodies/ota.h"


#define OTA_PERFORM_DELAY 2000  /* Milliseconds */


static os_timer_t              ota_timer;
static char                   *ota_url;
static uint8                   ota_ip_addr[4];
static ota_latest_callback_t   ota_latest_callback = NULL;
static ota_perform_callback_t  ota_perform_callback = NULL;
static ota_perform_callback_t  ota_auto_perform_callback = NULL;
static bool                    ota_auto_update_checking = FALSE;

static char                   *latest_url = NULL;
static char                   *latest_stable_url = NULL;
static char                   *latest_beta_url = NULL;
static char                   *url_template = NULL;
static char                   *current_version = NULL;


int         ICACHE_FLASH_ATTR ota_next_slot(void);

static void ICACHE_FLASH_ATTR on_ota_start(void *arg);
static void ICACHE_FLASH_ATTR on_ota_finish_check(void *arg);
static void ICACHE_FLASH_ATTR on_ota_latest_response(
                                  char *body,
                                  int body_len,
                                  int status,
                                  char *header_names[],
                                  char *header_values[],
                                  int header_count,
                                  uint8 addr[]
                              );
static void ICACHE_FLASH_ATTR on_ota_head_response(
                                  char *body,
                                  int body_len,
                                  int status,
                                  char *header_names[],
                                  char *header_values[],
                                  int header_count,
                                  uint8 addr[]
                              );

static void ICACHE_FLASH_ATTR on_ota_auto_latest(char *version, char *date, char *url);


void ota_init(char *cv, char *lu, char *lsu, char *lbu, char *ut) {
    free(current_version);
    current_version = strdup(cv);

    free(latest_url);
    latest_url = strdup(lu);

    free(latest_stable_url);
    latest_stable_url = strdup(lsu);

    free(latest_beta_url);
    latest_beta_url = strdup(lbu);

    free(url_template);
    url_template = strdup(ut);

    DEBUG_OTA("using latest URL \"%s\"", latest_url);
    DEBUG_OTA("using latest stable URL \"%s\"", latest_stable_url);
    DEBUG_OTA("using latest beta URL \"%s\"", latest_beta_url);
    DEBUG_OTA("using URL template \"%s\"", url_template);
}

bool ota_get_latest(bool beta, ota_latest_callback_t callback) {
    if (ota_latest_callback) {
        DEBUG_OTA("latest: we already have a pending request");
        return FALSE;
    }

    ota_latest_callback = callback;

    char *url = beta ? latest_url : latest_stable_url;

    DEBUG_OTA("fetching latest version from \"%s\"", url);

    httpclient_request(
        "GET",
        url,
        /* body = */ NULL,
        /* body_len = */ 0,
        /* header_names = */ NULL,
        /* header_values = */ NULL,
        /* header_count = */ 0,
        on_ota_latest_response,
        HTTP_DEF_TIMEOUT
    );

    return TRUE;
}

bool ota_perform_url(char *url, ota_perform_callback_t callback) {
    if (ota_busy()) {
        DEBUG_OTA("perform: currently busy");
        return FALSE;
    }

    /* OTA URL is the given URL + e.g. /user1.bin */
    int ota_usr = ota_next_slot();
    int len = strlen(url) + 11;  /* + extra /user1.bin */
    char *final_url = malloc(len);
    snprintf(final_url, len, "%s/user%d.bin", url, ota_usr);

    ota_perform_callback = callback;

    DEBUG_OTA("perform: downloading from %s", final_url);
    ota_url = final_url;

    httpclient_request(
        "HEAD",
        ota_url,
        /* body = */ NULL,
        /* body_len = */ 0,
        /* header_names = */ NULL,
        /* header_values = */ NULL,
        /* header_count = */ 0,
        on_ota_head_response,
        HTTP_DEF_TIMEOUT
    );

    return TRUE;
}

bool ota_perform_version(char *version, ota_perform_callback_t callback) {
    char url[256];

    snprintf(url, 256, url_template, version);
    return ota_perform_url(url, callback);
}

bool ota_auto_update_check(bool beta, ota_perform_callback_t callback) {
    if (ota_busy()) {
        DEBUG_OTA("auto-update: currently busy");
        return FALSE;
    }

    ota_auto_update_checking = TRUE;
    ota_auto_perform_callback = callback;

    DEBUG_OTA("auto-update: checking for a new version");

    ota_get_latest(beta, on_ota_auto_latest);

    return TRUE;
}

bool ota_busy(void) {
    return (system_upgrade_flag_check() == UPGRADE_FLAG_START) || ota_url ||
            ota_latest_callback || ota_auto_update_checking || ota_perform_callback;
}

int ota_current_state(void) {
    if (system_upgrade_flag_check() == UPGRADE_FLAG_IDLE) {
        if (ota_url || ota_latest_callback || ota_auto_update_checking || ota_perform_callback) {
            return OTA_STATE_CHECKING;
        }
        else {
            return OTA_STATE_IDLE;
        }
    }
    else if (system_upgrade_flag_check() == UPGRADE_FLAG_START) {
        return OTA_STATE_DOWNLOADING;
    }
    else  /* Assuming (system_upgrade_flag_check() == UPGRADE_FLAG_FINISH) */ {
        return OTA_STATE_RESTARTING;
    }
}


int ota_next_slot(void) {
    return !system_upgrade_userbin_check() + 1;
}

void on_ota_start(void *arg) {
    DEBUG_OTA("proceeding");

    struct upgrade_server_info *info = zalloc(sizeof(struct upgrade_server_info));

    info->pespconn = (struct espconn *) os_zalloc(sizeof(struct espconn));
    info->pespconn->proto.tcp = (esp_tcp *) os_zalloc(sizeof(esp_tcp));

    memcpy(info->ip, ota_ip_addr, 4);
    memcpy(info->pespconn->proto.tcp->remote_ip, ota_ip_addr, 4);
    info->pespconn->proto.tcp->remote_port = 80;
    info->port = 80;

    info->pespconn->type = ESPCONN_TCP;
    info->pespconn->state = ESPCONN_NONE;

    info->check_cb = on_ota_finish_check;
    info->check_times = 60000;
    info->url = (uint8 *) zalloc(512);

    char *host = ota_url;
    while (*host && *host != ':') {
        host++;
    }

    host += 3;  /* Skip :// */
    host = strdup(host);
    char *p = host;
    while (*p && *p != '/') {
        p++;
    }

    char *path = strdup(p);
    host[p - host] = 0;

    snprintf(
        (char *) info->url,
        512,
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: keep-alive\r\n"
        "\r\n",
        path,
        host
    );

    DEBUG_OTA("host = %s", host);
    DEBUG_OTA("path = %s", path);

    free(host);
    free(path);
    free(ota_url);
    ota_url = NULL;

    if (!system_upgrade_start(info)) {
        DEBUG_OTA("start failed");
        system_reset(/* delayed = */ FALSE);
    }
}

void on_ota_finish_check(void *arg) {
    struct upgrade_server_info *info = arg;
    if (info->upgrade_flag) {
        DEBUG_OTA("finished");
        system_upgrade_reboot();
    }
    else {
        DEBUG_OTA("failed");
        system_reset(/* delayed = */ FALSE);
    }

    free(info->pespconn->proto.tcp);
    free(info->pespconn);
    free(info->url);
    free(info);
}

void on_ota_latest_response(
    char *body,
    int body_len,
    int status,
    char *header_names[],
    char *header_values[],
    int header_count,
    uint8 addr[]
) {
    ota_latest_callback_t callback = ota_latest_callback;
    ota_latest_callback = NULL;

    if (status == 200) {
        char name[64] = {0};
        char value[256] = {0};
        char *pname = NULL, *pvalue = NULL;
        char *version = NULL;
        char *date = NULL;
        char *s = body;
        int c;
        bool in_name = TRUE;

        while ((c = *s++)) {
            if (c == ':') {  /* Name ends */
                pname = name;
                while (*pname && isspace((int) *pname)) {  /* Skip leading spaces */
                    pname++;
                }
                while (*pname && isspace((int) pname[strlen(pname)])) {  /* Remove trailing spaces */
                    pname[strlen(pname) - 1] = 0;
                }

                in_name = FALSE;
            }
            else if ((c == '\n') || (*s == 0)) {  /* Value ends */
                pvalue = value;
                while (*pvalue && isspace((int) *pvalue)) {  /* Skip leading spaces */
                    pvalue++;
                }
                while (*pvalue && isspace((int) pvalue[strlen(pvalue)])) {  /* Remove trailing spaces */
                    pvalue[strlen(pvalue) - 1] = 0;
                }

                if (pname && pvalue) {
                    if (!strcmp(pname, "Version")) {
                        version = strdup(pvalue);
                    }
                    else if (!strcmp(pname, "Date")) {
                        date = strdup(pvalue);
                    }
                }

                /* Reset name and value */
                name[0] = value[0] = 0;
                pname = pvalue = NULL;

                in_name = TRUE;
            }
            else {
                if (in_name) {
                    append_max_len(name, c, sizeof(name) - 1);
                }
                else {
                    append_max_len(value, c, sizeof(value) - 1);
                }
            }
        }

        DEBUG_OTA("latest: got version %s, date %s", version, date);

        char *url = malloc(256);
        snprintf(url, 256, url_template, version);

        if (callback) {
            callback(version, date, url);
        }
    }
    else {
        DEBUG_OTA("latest: got status %d", status);

        if (callback) {
            callback(NULL, NULL, NULL);
        }
    }
}

void on_ota_head_response(
    char *body,
    int body_len,
    int status,
    char *header_names[],
    char *header_values[],
    int header_count,
    uint8 addr[]
) {
    ota_perform_callback_t callback = ota_perform_callback;
    ota_perform_callback = NULL;

    if (status == 200) {
        DEBUG_OTA("head: file found");

        os_timer_disarm(&ota_timer);
        os_timer_setfn(&ota_timer, on_ota_start, NULL);
        os_timer_arm(&ota_timer, OTA_PERFORM_DELAY, /* repeat = */ FALSE);

        /* Remember resolved IP address, as we need it for actual OTA request */
        memcpy(ota_ip_addr, addr, 4);
    }
    else {
        DEBUG_OTA("head: got status %d", status);

        /* ota_url should already be set at this point */
        free(ota_url);
        ota_url = NULL;
    }

    if (callback) {
        callback(status);
    }
}

void on_ota_auto_latest(char *version, char *date, char *url) {
    ota_auto_update_checking = FALSE;

    if (version) {
        if (!strcmp(current_version, version)) {
            DEBUG_OTA("auto-update: already running latest version");
        }
        else if (strncmp(current_version, version, 2)) {  /* Major version differs */
            DEBUG_OTA("auto-update: refusing to automatically update to different major version");
        }
        else {
            DEBUG_OTA("auto-update: new version detected: %s", version);

            ota_perform_url(url, ota_auto_perform_callback);
        }

        free(version);
        free(date);
        free(url);
    }
    else {  /* Error */
        DEBUG_OTA("auto-update: failed to get latest version info");
    }
}
