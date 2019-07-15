
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

#ifndef _ESPGOODIES_OTA_H
#define _ESPGOODIES_OTA_H


#ifdef _DEBUG_OTA
#define DEBUG_OTA(fmt, ...)     DEBUG("[ota           ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_OTA(...)          {}
#endif

#define OTA_STATE_IDLE          0
#define OTA_STATE_CHECKING      1
#define OTA_STATE_DOWNLOADING   2
#define OTA_STATE_RESTARTING    3


/* version, date and url must be freed() by callback */
typedef void (*ota_latest_callback_t)(char *version, char *date, char *url);
typedef void (*ota_perform_callback_t)(int code);


ICACHE_FLASH_ATTR void          ota_init(char *current_version,char *latest_url, char *latest_beta_url,
                                         char *url_template);

ICACHE_FLASH_ATTR bool          ota_get_latest(bool beta, ota_latest_callback_t callback);
ICACHE_FLASH_ATTR bool          ota_perform_url(char *url, ota_perform_callback_t callback);
ICACHE_FLASH_ATTR bool          ota_perform_version(char *url, ota_perform_callback_t callback);
ICACHE_FLASH_ATTR bool          ota_auto_update_check(bool beta, ota_perform_callback_t callback);
ICACHE_FLASH_ATTR bool          ota_busy(void);
ICACHE_FLASH_ATTR int           ota_current_state(void);


#endif /* _ESPGOODIES_OTA_H */
