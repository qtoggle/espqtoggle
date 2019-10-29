
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


#define JSON_TYPE_NULL                  'n'
#define JSON_TYPE_BOOL                  'b'
#define JSON_TYPE_INT                   'i'
#define JSON_TYPE_DOUBLE                'd'
#define JSON_TYPE_STR                   's'
#define JSON_TYPE_LIST                  'l'
#define JSON_TYPE_OBJ                   'o'
#define JSON_TYPE_STRINGIFIED           't'
#define JSON_TYPE_MEMBERS_FREED         'f'

#define JSON_MAX_NAME_LEN               64
#define JSON_MAX_VALUE_LEN              1024
#define JSON_MAX_VALUE_LIST_LEN         4096

#define JSON_FREE_NOTHING               0
#define JSON_FREE_MEMBERS               1
#define JSON_FREE_EVERYTHING            2

#define JSON_ASSERT_TYPE(json, t)       {\
    if ((json)->type != t) DEBUG("unexpected JSON type at %s:%d: wanted %c, got %c",\
                                 __FILE__, __LINE__, t, (json)->type);\
}

#pragma pack(push, 1)

typedef struct json {

    char                    type;

    union {

        struct {
            uint16          len;
            struct json  ** children;
        } list_data;

        struct {
            uint8           len;
            char         ** keys;
            struct json  ** children;
        } obj_data;

        struct {
            uint16          len;
            char         ** chunks;
        } stringified_data;

        bool                bool_value;
        int                 int_value;
        double              double_value;
        char              * str_value;

    };

} json_t;

#pragma pack(pop)


#define                         json_get_type(json) ((json)->type)

ICACHE_FLASH_ATTR json_t      * json_parse(char *input);
ICACHE_FLASH_ATTR char        * json_dump(json_t *json, uint8 free_mode);
ICACHE_FLASH_ATTR char        * json_dump_r(json_t *json, uint8 free_mode);
ICACHE_FLASH_ATTR void          json_stringify(json_t *json);
ICACHE_FLASH_ATTR void          json_free(json_t *json);
ICACHE_FLASH_ATTR json_t      * json_dup(json_t *json);

ICACHE_FLASH_ATTR json_t      * json_null_new(void);
ICACHE_FLASH_ATTR json_t      * json_bool_new(bool value);
ICACHE_FLASH_ATTR json_t      * json_int_new(int value);
ICACHE_FLASH_ATTR json_t      * json_double_new(double value);
ICACHE_FLASH_ATTR json_t      * json_str_new(char *value);
#define                         json_bool_get(json) ({ \
                                    JSON_ASSERT_TYPE(json, JSON_TYPE_BOOL); \
                                    ((json)->bool_value); \
                                })

#define                         json_int_get(json) ({ \
                                    JSON_ASSERT_TYPE(json, JSON_TYPE_INT); \
                                    ((json)->int_value); \
                                })
#define                         json_double_get(json) ({ \
                                    JSON_ASSERT_TYPE(json, JSON_TYPE_DOUBLE); \
                                    ((json)->double_value); \
                                })
#define                         json_str_get(json) ({ \
                                    JSON_ASSERT_TYPE(json, JSON_TYPE_STR); \
                                    ((json)->str_value); \
                                })
#define                         json_list_value_at(json, index) ({ \
                                    JSON_ASSERT_TYPE(json, JSON_TYPE_LIST); \
                                    (json->list_data.children[(index)]); \
                                })
#define                         json_list_get_len(json) ({ \
                                    JSON_ASSERT_TYPE(json, JSON_TYPE_LIST); \
                                    (json->list_data.len); \
                                })

ICACHE_FLASH_ATTR json_t      * json_list_new(void);
ICACHE_FLASH_ATTR void          json_list_append(json_t *json, json_t *child);
ICACHE_FLASH_ATTR json_t      * json_list_pop_at(json_t *json, uint32 index);
#define                         json_list_value_at(json, index) ({ \
                                    JSON_ASSERT_TYPE(json, JSON_TYPE_LIST); \
                                    (json->list_data.children[(index)]); \
                                })
#define                         json_list_get_len(json) ({ \
                                    JSON_ASSERT_TYPE(json, JSON_TYPE_LIST); \
                                    (json->list_data.len); \
                                })

ICACHE_FLASH_ATTR json_t      * json_obj_lookup_key(json_t *json, char *key);
ICACHE_FLASH_ATTR json_t      * json_obj_pop_key(json_t *json, char *key);
ICACHE_FLASH_ATTR json_t      * json_obj_new(void);
ICACHE_FLASH_ATTR void          json_obj_append(json_t *json, char *key, json_t *child);
#define                         json_obj_key_at(json, index) ({ \
                                    JSON_ASSERT_TYPE(json, JSON_TYPE_OBJ); \
                                    (json->obj_data.keys[(index)]); \
                                })
#define                         json_obj_value_at(json, index) ({ \
                                    JSON_ASSERT_TYPE(json, JSON_TYPE_OBJ); \
                                    (json->obj_data.children[(index)]); \
                                })
#define                         json_obj_get_len(json) ({ \
                                    JSON_ASSERT_TYPE(json, JSON_TYPE_OBJ); \
                                    (json->obj_data.len); \
                                })


#endif /* _ESPGOODIES_JSON_H */
