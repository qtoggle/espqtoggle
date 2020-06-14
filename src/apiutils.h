
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


#define EMPTY_SHA256_HEX "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"


double ICACHE_FLASH_ATTR  get_choice_value_num(char *choice);
char   ICACHE_FLASH_ATTR *get_choice_value_str(char *choice);
char   ICACHE_FLASH_ATTR *get_choice_display_name(char *choice);
json_t ICACHE_FLASH_ATTR *choice_to_json(char *choice, char type);
void   ICACHE_FLASH_ATTR  free_choices(char **choices);
bool   ICACHE_FLASH_ATTR  choices_equal(char **choices1, char **choices2);

int    ICACHE_FLASH_ATTR  validate_num(double value, double min, double max, bool integer, double step, char **choices);
int    ICACHE_FLASH_ATTR  validate_str(char *value, char **choices);
bool   ICACHE_FLASH_ATTR  validate_id(char *id);
bool   ICACHE_FLASH_ATTR  validate_ip_address(char *ip, uint8 *a);
bool   ICACHE_FLASH_ATTR  validate_wifi_ssid(char *ssid);
bool   ICACHE_FLASH_ATTR  validate_wifi_key(char *key);
bool   ICACHE_FLASH_ATTR  validate_wifi_bssid(char *bssid_str, uint8 *bssid);

json_t ICACHE_FLASH_ATTR *attrdef_to_json(
                              char *display_name,
                              char *description,
                              char *unit,
                              char type,
                              bool modifiable,
                              double min,
                              double max,
                              bool integer,
                              double step,
                              char **choices,
                              bool reconnect
                          );


#endif /* _API_UTILS_H */
