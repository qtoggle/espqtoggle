
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

#ifndef _ESPGOODIES_OTA_H
#define _ESPGOODIES_OTA_H


#ifdef _DEBUG_OTA
#define DEBUG_OTA(fmt, ...) DEBUG("[ota           ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_OTA(...)      {}
#endif

#define OTA_STATE_IDLE        0
#define OTA_STATE_CHECKING    1
#define OTA_STATE_DOWNLOADING 2
#define OTA_STATE_RESTARTING  3


/* Version, date and url must be freed() by callback */
typedef void (*ota_latest_callback_t)(char *version, char *date, char *url);
typedef void (*ota_perform_callback_t)(int code);


ICACHE_FLASH_ATTR void ota_init(
                           char *current_version,
                           char *latest_url,
                           char *latest_stable_url,
                           char *latest_beta_url,
                           char *url_template
                       );

ICACHE_FLASH_ATTR bool ota_get_latest(bool beta, ota_latest_callback_t callback);
ICACHE_FLASH_ATTR bool ota_perform_url(char *url, ota_perform_callback_t callback);
ICACHE_FLASH_ATTR bool ota_perform_version(char *url, ota_perform_callback_t callback);
ICACHE_FLASH_ATTR bool ota_auto_update_check(bool beta, ota_perform_callback_t callback);
ICACHE_FLASH_ATTR bool ota_busy(void);
ICACHE_FLASH_ATTR int  ota_current_state(void);


#endif /* _ESPGOODIES_OTA_H */
