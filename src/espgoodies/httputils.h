
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

#ifndef _ESPGOODIES_HTTPUTILS_H
#define _ESPGOODIES_HTTPUTILS_H


#include "espgoodies/json.h"


json_t ICACHE_FLASH_ATTR *http_parse_url_encoded(char *input);
char   ICACHE_FLASH_ATTR *http_build_auth_token_header(char *token);
char   ICACHE_FLASH_ATTR *http_parse_auth_token_header(char *header);


#endif /* _ESPGOODIES_HTTPUTILS_H */
