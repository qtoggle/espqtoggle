
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

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <mem.h>

#include "common.h"
#include "utils.h"
#include "json.h"


ICACHE_FLASH_ATTR static void   json_dump_rec(json_t *json, char **output, int *len, int *size, bool also_free);


/* !!! WARNING: this is a small implementation that only works with one object on the root level,
 *              and one level depth of objects and lists, in general !!! */
json_t *json_parse(char *input) {
    char name[JSON_MAX_NAME_LEN + 1];
    char value[JSON_MAX_VALUE_LIST_LEN + 1];
    name[0] = 0;
    value[0] = 0;

    char *s = input;
    int c, pc = 0, state = 0;
    bool quotes = FALSE; /* inside double quotes, used for string values */
    bool brackets = FALSE; /* inside brackets, used for list values */
    bool had_quotes = FALSE; /* true for values that are in quotes */
    bool had_brackets = FALSE; /* true for values that are in brackets */
    
    /* skip any leading spaces */
    while ((int) isspace((int) s[0])) {
        s++;
    }

    /* wrap any non-dictionary input in a dummy dictionary, as a workaround */
    char *tmp_input = NULL;
    if (s[0] != '{') {
        int len = strlen(input) + 7;
        tmp_input = malloc(len);
        snprintf(tmp_input, len, "{\"v\":%s}", s);
        s = tmp_input;
    }

    json_t *root = json_obj_new();

    while ((c = *s++)) {
        switch (state) {
            case 0: { /* outside fields */
                if (c == '"') {
                    state = 1; /* name */
                }

                break;
            }

            case 1: { /* name */
                if (c == '"') { /* name ends */
                    state = 2; /* between name and value */
                }
                else {
                    append_max_len(name, c, JSON_MAX_NAME_LEN);
                }

                break;
            }

            case 2: { /* between name and value */
                if (c == ':') { /* value starts */
                    state = 3; /* value */
                }

                break;

            case 3: /* value */
                if (quotes) {
                    if (c == '\\') {
                        if (pc == '\\') { /* escaped backslash */
                            append_max_len(value, c, JSON_MAX_VALUE_LIST_LEN);
                        }
                    }
                    else if (c == '"' && !brackets) {
                        if (pc == '\\') { /* escaped double quote */
                            append_max_len(value, c, JSON_MAX_VALUE_LIST_LEN);
                        }
                        else if (c == ',') { /* comma inside quoted value */
                            append_max_len(value, c, JSON_MAX_VALUE_LIST_LEN);
                        }
                        else { /* closing quotes */
                            quotes = FALSE;
                            had_quotes = TRUE;
                        }
                    }
                    else {
                        append_max_len(value, c, JSON_MAX_VALUE_LIST_LEN);
                    }
                }
                else { /* outside quotes */
                    if (c == '"' && !brackets) { /* opening quotes */
                        quotes = TRUE;
                    }
                    else if (isspace(c)) {
                        /* ignore spaces outside of quotes */
                    }
                    else if ((c == ',' && !brackets) || c == '}') { /* comma as field separator or object end */
                        if (had_quotes) { /* string */
                            json_obj_append(root, name, json_str_new(value));
                        }
                        else if (had_brackets) { /* list */
                            json_t *child = json_list_new();
                            int q = 0, hq = 0, e = 0, i = 0;
                            char v[JSON_MAX_VALUE_LEN + 1] = {0};
                            append_max_len(value, ',', JSON_MAX_VALUE_LIST_LEN); /* always terminate a list with comma */
                            while ((c = value[i])) {
                                if (e) {
                                    append_max_len(v, c, JSON_MAX_VALUE_LEN);
                                    e = 0;
                                    i++;
                                    continue;
                                }

                                if (c == '\\') {
                                    e = 1;
                                }
                                else if (c == ',') {
                                    if (hq) {
                                        json_list_append(child, json_str_new(v));
                                    }
                                    else if (!strcmp(v, "null")) {
                                        json_list_append(child, json_null_new());
                                    }
                                    else if (!strcmp(v, "false")) {
                                        json_list_append(child, json_bool_new(FALSE));
                                    }
                                    else if (!strcmp(v, "true")) {
                                        json_list_append(child, json_bool_new(TRUE));
                                    }
                                    else if (v[0]) {
                                        if (strchr(v, '.')) {
                                            json_list_append(child, json_double_new(strtod(v, NULL)));
                                        }
                                        else {
                                            json_list_append(child, json_int_new(strtol(v, NULL, 10)));
                                        }
                                    }

                                    v[0] = q = hq = e = 0;
                                }
                                else if (c == '"') {
                                    q = !q;
                                    hq = 1;
                                } 
                                else {
                                    append_max_len(v, c, JSON_MAX_VALUE_LEN);
                                }

                                i++;
                            }

                            json_obj_append(root, name, child);
                        }
                        else if (!strcmp(value, "null")) {
                            json_obj_append(root, name, json_null_new());
                        }
                        else if (!strcmp(value, "false")) {
                            json_obj_append(root, name, json_bool_new(FALSE));
                        }
                        else if (!strcmp(value, "true")) {
                            json_obj_append(root, name, json_bool_new(TRUE));
                        }
                        else if (strchr(value, '.')) { /* double */
                            json_obj_append(root, name, json_double_new(strtod(value, NULL)));
                        }
                        else { /* assuming integer */
                            json_obj_append(root, name, json_int_new(strtol(value, NULL, 10)));
                        }

                        state = 0; /* outside fields */
                        name[0] = 0;
                        value[0] = 0;
                        had_quotes = FALSE;
                        had_brackets = FALSE;
                    }
                    else if (c == '[' || c == ']') {
                        brackets = !brackets;
                        had_brackets = TRUE;
                    }
                    else {
                        append_max_len(value, c, JSON_MAX_VALUE_LIST_LEN);
                    }
                }

                break;
            }
        }

        pc = c;
    }
    
    if (tmp_input) {
        json_t *tmp_root = json_dup(json_obj_lookup_key(root, "v"));
        json_free(root);
        root = tmp_root;
        free(tmp_input);
    }

    return root;
}

char *json_dump(json_t *json, bool also_free) {
    char *output = NULL;
    int len = 0, size = 0;
    
    json_dump_rec(json, &output, &len, &size, also_free);
    size = realloc_chunks(&output, size, len + 1);
    output[len] = 0;

    return output;
}

void json_stringify(json_t *json) {
    if (json->type == JSON_TYPE_STRINGIFIED) {
        return; /* already stringified */
    }

    int i;
    char *stringified = json_dump(json, FALSE /* also_free */);

    /* partial free */
    switch (json->type) {
        case JSON_TYPE_NULL:
        case JSON_TYPE_BOOL:
        case JSON_TYPE_INT:
        case JSON_TYPE_DOUBLE:
            break;

        case JSON_TYPE_STR:
            free(json->str_value);
            break;

        case JSON_TYPE_LIST:
            for (i = 0; i < json->list_data.len; i++) {
                json_free(json->list_data.children[i]);
            }
            free(json->list_data.children);
            break;

        case JSON_TYPE_OBJ:
            for (i = 0; i < json->obj_data.len; i++) {
                free(json->obj_data.keys[i]);
                json_free(json->obj_data.children[i]);
            }
            free(json->obj_data.keys);
            free(json->obj_data.children);
            break;
    }

    json->str_value = stringified;
    json->type = JSON_TYPE_STRINGIFIED;
}

void json_free(json_t *json) {
    int i;

    switch (json->type) {
        case JSON_TYPE_NULL:
        case JSON_TYPE_BOOL:
        case JSON_TYPE_INT:
        case JSON_TYPE_DOUBLE:
            free(json);
            break;

        case JSON_TYPE_STR:
        case JSON_TYPE_STRINGIFIED:
            free(json->str_value);
            free(json);
            break;

        case JSON_TYPE_LIST:
            for (i = 0; i < json->list_data.len; i++) {
                json_free(json->list_data.children[i]);
            }
            free(json->list_data.children);
            free(json);
            break;

        case JSON_TYPE_OBJ:
            for (i = 0; i < json->obj_data.len; i++) {
                free(json->obj_data.keys[i]);
                json_free(json->obj_data.children[i]);
            }
            free(json->obj_data.keys);
            free(json->obj_data.children);
            free(json);
            break;
    }
}

json_t *json_obj_lookup_key(json_t *json, char *key) {
    int i;
    for (i = 0; i < json->obj_data.len; i++) {
        if (!strcmp(key, json->obj_data.keys[i])) {
            return json->obj_data.children[i];
        }
    }

    return NULL;
}

json_t *json_null_new() {
    json_t *json = malloc(sizeof(json_t));
    json->type = JSON_TYPE_NULL;
    
    return json;
}

json_t *json_bool_new(bool value) {
    json_t *json = malloc(sizeof(json_t));
    json->type = JSON_TYPE_BOOL;
    json->bool_value = value;

    return json;
}

json_t *json_int_new(int value) {
    json_t *json = malloc(sizeof(json_t));
    json->type = JSON_TYPE_INT;
    json->int_value = value;

    return json;
}

json_t *json_double_new(double value) {
    json_t *json = malloc(sizeof(json_t));
    json->type = JSON_TYPE_DOUBLE;
    json->double_value = value;

    return json;
}

json_t *json_str_new(char *value) {
    json_t *json = malloc(sizeof(json_t));
    json->type = JSON_TYPE_STR;
    json->str_value = (void *) strdup(value);

    return json;
}

json_t *json_list_new() {
    json_t *json = malloc(sizeof(json_t));
    json->type = JSON_TYPE_LIST;

    json->list_data.len = 0;
    json->list_data.children = NULL;

    return json;
}

void json_list_append(json_t *json, json_t *child) {
    json->list_data.children = realloc(json->list_data.children, sizeof(json_t *) * (json->list_data.len + 1));
    json->list_data.children[(int) json->list_data.len++] = child;
}

json_t *json_obj_new() {
    json_t *json = malloc(sizeof(json_t));
    json->type = JSON_TYPE_OBJ;

    json->obj_data.len = 0;
    json->obj_data.keys = NULL;
    json->obj_data.children = NULL;

    return json;
}

void json_obj_append(json_t *json, char *key, json_t *child) {
    json->obj_data.children = realloc(json->obj_data.children, sizeof(json_t *) * (json->obj_data.len + 1));
    json->obj_data.keys = realloc(json->obj_data.keys, sizeof(char *) * (json->obj_data.len + 1));
    json->obj_data.keys[(int) json->obj_data.len] = strdup(key);
    json->obj_data.children[(int) json->obj_data.len++] = child;
}


void json_dump_rec(json_t *json, char **output, int *len, int *size, bool also_free) {
    int i, l;
    char s[32];
    
    switch (json->type) {
        case JSON_TYPE_NULL:
            *size = realloc_chunks(output, *size, *len + 4);
            strncpy(*output + *len, "null", 5);
            *len += 4;

            break;

        case JSON_TYPE_BOOL:
            if (json_bool_get(json)) {
                *size = realloc_chunks(output, *size, *len + 4);
                strncpy(*output + *len, "true", 5);
                *len += 4;
            }
            else {
                *size = realloc_chunks(output, *size, *len + 5);
                strncpy(*output + *len, "false", 6);
                *len += 5;
            }
            
            break;

        case JSON_TYPE_INT:
            l = snprintf(s, 16, "%d", json_int_get(json));
            *size = realloc_chunks(output, *size, *len + l);
            strncpy(*output + *len, s, l);
            *len += l;

            break;

        case JSON_TYPE_DOUBLE:
            strcpy(s, dtostr(json_double_get(json), -1));
            l = strlen(s);
            *size = realloc_chunks(output, *size, *len + l);
            strncpy(*output + *len, s, l);
            *len += l;

            break;

        case JSON_TYPE_STR:
            // TODO escapes
            l = strlen(json_str_get(json)) + 2;
            *size = realloc_chunks(output, *size, *len + l);
            (*output)[*len] = '"';
            strncpy(*output + *len + 1, json_str_get(json), l - 2);
            *len += l;
            (*output)[*len - 1] = '"';

            break;

        case JSON_TYPE_LIST:
            *size = realloc_chunks(output, *size, *len + 1);
            (*output)[(*len)++] = '[';
            for (i = 0; i < json->list_data.len; i++) {
                json_dump_rec(json->list_data.children[i], output, len, size, also_free);
                if (i < json->list_data.len - 1) {
                    *size = realloc_chunks(output, *size, *len + 1);
                    (*output)[(*len)++] = ',';
                }
            }
            *size = realloc_chunks(output, *size, *len + 1);
            (*output)[(*len)++] = ']';

            if (also_free) {
                json->list_data.len = 0;
            }

            break;

        case JSON_TYPE_OBJ:
            *size = realloc_chunks(output, *size, *len + 1);
            (*output)[(*len)++] = '{';
            for (i = 0; i < json->obj_data.len; i++) {
                l = strlen(json->obj_data.keys[i]) + 3;
                *size = realloc_chunks(output, *size, *len + l);
                (*output)[*len] = '"';
                strncpy(*output + *len + 1, json->obj_data.keys[i], l - 3);
                *len += l;
                (*output)[*len - 2] = '"';
                (*output)[*len - 1] = ':';

                json_dump_rec(json->obj_data.children[i], output, len, size, also_free);
                if (i < json->obj_data.len - 1) {
                    *size = realloc_chunks(output, *size, *len + 1);
                    (*output)[(*len)++] = ',';
                }

                if (also_free) {
                    free(json->obj_data.keys[i]);
                }
            }
            *size = realloc_chunks(output, *size, *len + 1);
            (*output)[(*len)++] = '}';

            if (also_free) {
                json->obj_data.len = 0;
            }

            break;

        case JSON_TYPE_STRINGIFIED:
            l = strlen(json_str_get(json));
            *size = realloc_chunks(output, *size, *len + l);
            strncpy(*output + *len, json_str_get(json), l);
            *len += l;

            break;
    }

    if (also_free) {
        json_free(json);
    }
}

json_t *json_dup(json_t *json) {
    switch (json->type) {
        case JSON_TYPE_NULL:
            return json_null_new();

        case JSON_TYPE_BOOL:
            return json_bool_new(json_bool_get(json));

        case JSON_TYPE_INT:
            return json_int_new(json_int_get(json));

        case JSON_TYPE_DOUBLE:
            return json_double_new(json_double_get(json));

        case JSON_TYPE_STR:
            return json_str_new(json_str_get(json));

        case JSON_TYPE_LIST: {
            json_t *list = json_list_new();
            int i;
            for (i = 0; i < json->list_data.len; i++) {
                json_list_append(list, json_dup(json->list_data.children[i]));
            }
            
            return list;
        }

        case JSON_TYPE_OBJ: {
            json_t *obj = json_obj_new();
            int i;
            for (i = 0; i < json->obj_data.len; i++) {
                json_obj_append(obj,
                                json->obj_data.keys[i],
                                json_dup(json->obj_data.children[i]));
            }

            return obj;
        }
        
        case JSON_TYPE_STRINGIFIED: {
            json_t *new_json = malloc(sizeof(json_t));
            new_json->type = JSON_TYPE_STRINGIFIED;
            new_json->str_value = strdup(json->str_value);

            return new_json;
        }

        default:
            return NULL;
    }
}

