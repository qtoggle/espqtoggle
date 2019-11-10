
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

#ifndef _API_UTILS_H
#define _API_UTILS_H


#include <ip_addr.h>
#include <espconn.h>

#include "espgoodies/common.h"
#include "espgoodies/json.h"

#include "ports.h"


#define EMPTY_SHA256_HEX                    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"


ICACHE_FLASH_ATTR double                    get_choice_value_num(char *choice);
ICACHE_FLASH_ATTR char                    * get_choice_value_str(char *choice);
ICACHE_FLASH_ATTR char                    * get_choice_display_name(char *choice);
ICACHE_FLASH_ATTR json_t                  * choice_to_json(char *choice, char type);
ICACHE_FLASH_ATTR void                      free_choices(char **choices);
ICACHE_FLASH_ATTR bool                      choices_equal(char **choices1, char **choices2);

ICACHE_FLASH_ATTR bool                      validate_num(double value, double min, double max, bool integer,
                                                         double step, char **choices);
ICACHE_FLASH_ATTR bool                      validate_str(char *value, char **choices);
ICACHE_FLASH_ATTR bool                      validate_id(char *id);
ICACHE_FLASH_ATTR bool                      validate_str_ip(char *ip, uint8 *a, int len);
ICACHE_FLASH_ATTR bool                      validate_str_wifi(char *wifi, char *ssid, char *psk, uint8 *bssid);
ICACHE_FLASH_ATTR bool                      validate_str_network_scan(char *scan, int *scan_interval,
                                                                      int *scan_threshold);
#ifdef _SLEEP
ICACHE_FLASH_ATTR bool                      validate_str_sleep_mode(char *sleep_mode, int *wake_interval,
                                                                    int *wake_duration);
#endif

ICACHE_FLASH_ATTR json_t                  * attrdef_to_json(char *display_name, char *description, char *unit,
                                                            char type, bool modifiable, double min, double max,
                                                            bool integer, double step, char **choices, bool reconnect);


ICACHE_FLASH_ATTR json_t                  * make_json_ref(const char *target_fmt, ...);

#endif /* _API_UTILS_H */
