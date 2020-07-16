
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

#ifndef _JSON_REFS_H
#define _JSON_REFS_H


#include "espgoodies/common.h"
#include "espgoodies/json.h"

#include "ports.h"


#define JSON_REFS_TYPE_PORT               1
#define JSON_REFS_TYPE_PORTS_LIST         2
#define JSON_REFS_TYPE_LISTEN_EVENTS_LIST 3
#define JSON_REFS_TYPE_WEBHOOKS_EVENT     4


typedef struct {

    uint8 type;
    uint8 index;

    int8  sampling_interval_port_index;

} json_refs_ctx_t;


json_t ICACHE_FLASH_ATTR *make_json_ref(const char *target_fmt, ...);
void   ICACHE_FLASH_ATTR  lookup_port_attrdef_choices(
                              char **choices,
                              port_t *port,
                              attrdef_t *attrdef,
                              int8 *found_port_index,
                              char **found_attrdef_name,
                              json_refs_ctx_t *json_refs_ctx
                          );

void   ICACHE_FLASH_ATTR  json_refs_ctx_init(json_refs_ctx_t *json_refs_ctx, uint8 type);


#endif /* _JSON_REFS_H */
