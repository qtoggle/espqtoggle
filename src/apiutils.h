
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

#include "espgoodies/json.h"

#include "ports.h"


#define EMPTY_SHA256_HEX                    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"


ICACHE_FLASH_ATTR double                    get_choice_value_num(char *choice);
ICACHE_FLASH_ATTR char                    * get_choice_value_str(char *choice);
ICACHE_FLASH_ATTR char                    * get_choice_display_name(char *choice);


#endif /* _API_UTILS_H */
