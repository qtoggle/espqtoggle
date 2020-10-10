
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

#ifndef _ESPGOODIES_JSON_H
#define _ESPGOODIES_JSON_H


#include <os_type.h>


#define JSON_TYPE_NULL          'n'
#define JSON_TYPE_BOOL          'b'
#define JSON_TYPE_INT           'i'
#define JSON_TYPE_DOUBLE        'd'
#define JSON_TYPE_STR           's'
#define JSON_TYPE_LIST          'l'
#define JSON_TYPE_OBJ           'o'
#define JSON_TYPE_STRINGIFIED   't'
#define JSON_TYPE_MEMBERS_FREED 'f'

#define JSON_MAX_NAME_LEN       64
#define JSON_MAX_VALUE_LEN      1024
#define JSON_MAX_VALUE_LIST_LEN 4096

#define JSON_FREE_NOTHING       0
#define JSON_FREE_MEMBERS       1
#define JSON_FREE_EVERYTHING    2

#if defined(_DEBUG) && defined(_DEBUG_JSON)
#define DEBUG_JSON(fmt, ...) DEBUG("[json          ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_JSON(...)      {}
#endif


typedef struct json {

    union {

        struct {
            char        **keys;
            struct json **children;
        };

        char            **chunks;
        bool              bool_value;
        int32             int_value;
        double            double_value;
        char             *str_value;

    };

    uint16                len;
    char                  type;

} json_t;


json_t ICACHE_FLASH_ATTR *json_parse(char *input);
char   ICACHE_FLASH_ATTR *json_dump(json_t *json, uint8 free_mode);
char   ICACHE_FLASH_ATTR *json_dump_r(json_t *json, uint8 free_mode);
void   ICACHE_FLASH_ATTR  json_stringify(json_t *json);
void   ICACHE_FLASH_ATTR  json_free(json_t *json);
json_t ICACHE_FLASH_ATTR *json_dup(json_t *json);

char   ICACHE_FLASH_ATTR  json_get_type(json_t *json);
void   ICACHE_FLASH_ATTR  json_assert_type(json_t *json, char type);

json_t ICACHE_FLASH_ATTR *json_null_new(void);

json_t ICACHE_FLASH_ATTR *json_bool_new(bool value);
bool   ICACHE_FLASH_ATTR  json_bool_get(json_t *json);

json_t ICACHE_FLASH_ATTR *json_int_new(int value);
int32  ICACHE_FLASH_ATTR  json_int_get(json_t *json);

json_t ICACHE_FLASH_ATTR *json_double_new(double value);
double ICACHE_FLASH_ATTR  json_double_get(json_t *json);

json_t ICACHE_FLASH_ATTR *json_str_new(char *value);
char   ICACHE_FLASH_ATTR *json_str_get(json_t *json);

json_t ICACHE_FLASH_ATTR *json_list_new(void);
void   ICACHE_FLASH_ATTR  json_list_append(json_t *json, json_t *child);
json_t ICACHE_FLASH_ATTR *json_list_value_at(json_t *json, uint32 index);
json_t ICACHE_FLASH_ATTR *json_list_pop_at(json_t *json, uint32 index);
uint32 ICACHE_FLASH_ATTR  json_list_get_len(json_t *json);

json_t ICACHE_FLASH_ATTR *json_obj_lookup_key(json_t *json, char *key);
json_t ICACHE_FLASH_ATTR *json_obj_pop_key(json_t *json, char *key);
json_t ICACHE_FLASH_ATTR *json_obj_new(void);
void   ICACHE_FLASH_ATTR  json_obj_append(json_t *json, char *key, json_t *child);
char   ICACHE_FLASH_ATTR *json_obj_key_at(json_t *json, uint32 index);
json_t ICACHE_FLASH_ATTR *json_obj_value_at(json_t *json, uint32 index);
json_t ICACHE_FLASH_ATTR *json_obj_pop_at(json_t *json, uint32 index);
uint32 ICACHE_FLASH_ATTR  json_obj_get_len(json_t *json);


#endif /* _ESPGOODIES_JSON_H */
