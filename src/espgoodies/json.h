
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

#ifndef _ESPGOODIES_JSON_H
#define _ESPGOODIES_JSON_H


#include <os_type.h>


#define JSON_TYPE_NULL                  1
#define JSON_TYPE_BOOL                  2
#define JSON_TYPE_INT                   3
#define JSON_TYPE_DOUBLE                4
#define JSON_TYPE_STR                   5
#define JSON_TYPE_LIST                  6
#define JSON_TYPE_OBJ                   7
#define JSON_TYPE_STRINGIFIED           10

#define JSON_MAX_NAME_LEN               64
#define JSON_MAX_VALUE_LEN              1024
#define JSON_MAX_VALUE_LIST_LEN         4096


typedef struct json {

    char                    type;

    union {

        struct {
            uint16          len;
            struct json **  children;
        } list_data;

        struct {
            uint8           len;
            char **         keys;
            struct json **  children;
        } obj_data;

        bool                bool_value;
        int                 int_value;
        double              double_value;
        char              * str_value;

    };

} json_t;


ICACHE_FLASH_ATTR json_t *      json_parse(char *input);
ICACHE_FLASH_ATTR char *        json_dump(json_t *json, bool also_free);
ICACHE_FLASH_ATTR void          json_stringify(json_t *json);
ICACHE_FLASH_ATTR void          json_free(json_t *json);

#define                         json_get_type(json) ((json)->type)
#define                         json_bool_get(json) ((json)->bool_value)
#define                         json_int_get(json) ((json)->int_value)
#define                         json_double_get(json) ((json)->double_value)
#define                         json_str_get(json) ((json)->str_value)
#define                         json_list_value_at(json, index) (json->list_data.children[(index)])
#define                         json_list_get_len(json) (json->list_data.len)
#define                         json_obj_key_at(json, index) (json->obj_data.keys[(index)])
#define                         json_obj_value_at(json, index) (json->obj_data.children[(index)])
#define                         json_obj_get_len(json) (json->obj_data.len)
ICACHE_FLASH_ATTR json_t *      json_obj_lookup_key(json_t *json, char *key);

ICACHE_FLASH_ATTR json_t *      json_null_new();
ICACHE_FLASH_ATTR json_t *      json_bool_new(bool value);
ICACHE_FLASH_ATTR json_t *      json_int_new(int value);
ICACHE_FLASH_ATTR json_t *      json_double_new(double value);
ICACHE_FLASH_ATTR json_t *      json_str_new(char *value);
ICACHE_FLASH_ATTR json_t *      json_list_new();
ICACHE_FLASH_ATTR void          json_list_append(json_t *json, json_t *child);
ICACHE_FLASH_ATTR json_t *      json_obj_new();
ICACHE_FLASH_ATTR void          json_obj_append(json_t *json, char *key, json_t *child);
ICACHE_FLASH_ATTR json_t *      json_dup(json_t *json);


#endif /* _ESPGOODIES_JSON_H */
