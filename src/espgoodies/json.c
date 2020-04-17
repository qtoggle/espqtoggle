
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


#define ctx_get_size(ctx)           ((ctx)->stack_size)
#define ctx_has_key(ctx)            ((ctx)->stack_size > 0 && (ctx)->stack[(ctx)->stack_size - 1].key != NULL)
#define ctx_get_key(ctx)            ((ctx)->stack_size > 0 ? (ctx)->stack[(ctx)->stack_size - 1].key : NULL)

#define STRINGIFIED_CHUNK_SIZE      128


typedef struct {

    json_t    * json;
    char      * key;

} stack_t;

typedef struct {

    uint32      stack_size;
    stack_t   * stack;

} ctx_t;


ICACHE_FLASH_ATTR static void       json_dump_rec(json_t *json, char **output, int *len, int *size, uint8 free_mode);

ICACHE_FLASH_ATTR static ctx_t    * ctx_new(char *input);
ICACHE_FLASH_ATTR static void       ctx_set_key(ctx_t *ctx, char *key);
ICACHE_FLASH_ATTR static void       ctx_clear_key(ctx_t *ctx);
ICACHE_FLASH_ATTR static json_t   * ctx_get_current(ctx_t *ctx);
ICACHE_FLASH_ATTR static bool       ctx_add(ctx_t *ctx, json_t *json);
ICACHE_FLASH_ATTR static void       ctx_push(ctx_t *ctx, json_t *json);
ICACHE_FLASH_ATTR static json_t   * ctx_pop(ctx_t *ctx);
ICACHE_FLASH_ATTR static void       ctx_free(ctx_t *ctx);


json_t *json_parse(char *input) {
    char c, c2;
    char s[JSON_MAX_VALUE_LEN + 1];
    bool end_found, point_seen, waiting_elem = TRUE;
    uint32 i, sl, pos = 0;
    uint32 length = strlen(input);
    ctx_t *ctx = ctx_new(input);
    json_t *json, *root = NULL;

    /* Remove all whitespace at the end of input */
    while (isspace((int) input[length - 1])) {
        length--;
    }

    while (pos < length) {
        c = input[pos];

        /* If root already popped, we don't expect any more characters */
        if (root) {
            DEBUG("unexpected character \"%c\" at pos %d", c, pos);
            json_free(root);
            ctx_free(ctx);
            return NULL;
        }

        switch (c) {
            case '{':
                if (!waiting_elem) {
                    DEBUG("unexpected character \"%c\" at pos %d", c, pos);
                    ctx_free(ctx);
                    return NULL;
                }

                ctx_push(ctx, json_obj_new());

                /* Waiting for a key, not an element */
                waiting_elem = FALSE;

                pos++;
                break;

            case '}':
                json = ctx_get_current(ctx);
                if (!json || json_get_type(json) != JSON_TYPE_OBJ ||
                    (waiting_elem && json_obj_get_len(json) > 0) || ctx_has_key(ctx)) {

                    DEBUG("unexpected character \"%c\" at pos %d", c, pos);
                    ctx_free(ctx);
                    return NULL;
                }

                waiting_elem = FALSE;
                root = ctx_pop(ctx);
                pos++;
                break;

            case '[':
                if (!waiting_elem) {
                    DEBUG("unexpected character \"%c\" at pos %d", c, pos);
                    ctx_free(ctx);
                    return NULL;
                }

                ctx_push(ctx, json_list_new());
                pos++;
                break;

            case ']':
                json = ctx_get_current(ctx);
                if (!json || json_get_type(json) != JSON_TYPE_LIST ||
                    (waiting_elem && json_list_get_len(json) > 0)) {

                    DEBUG("unexpected character \"%c\" at pos %d", c, pos);
                    ctx_free(ctx);
                    return NULL;
                }

                waiting_elem = FALSE;
                root = ctx_pop(ctx);
                pos++;
                break;

            case ',':
                json = ctx_get_current(ctx);
                if (!json || waiting_elem) {
                    DEBUG("unexpected character \"%c\" at pos %d", c, pos);
                    ctx_free(ctx);
                    return NULL;
                }

                if (json_get_type(json) == JSON_TYPE_LIST) {
                    waiting_elem = TRUE;
                }

                pos++;
                break;

            case ':':
                json = ctx_get_current(ctx);
                if (!json || json_get_type(json) != JSON_TYPE_OBJ || !ctx_has_key(ctx) || waiting_elem) {
                    DEBUG("unexpected character \"%c\" at pos %d", c, pos);
                    ctx_free(ctx);
                    return NULL;
                }

                waiting_elem = TRUE;

                pos++;
                break;

            case ' ':
            case '\b':
            case '\f':
            case '\n':
            case '\r':
            case '\t':
                /* Skip whitespace */
                pos++;
                break;

            case '"':
                /* Parse a string, which can be either a standalone string element or an object key */

                json = ctx_get_current(ctx);
                if (json && json_get_type(json) == JSON_TYPE_OBJ) {
                    if (ctx_has_key(ctx) != waiting_elem) {
                        DEBUG("unexpected character \"%c\" at pos %d", c, pos);
                        ctx_free(ctx);
                        return NULL;
                    }
                }
                else { /* No parent or not an object */
                    if (!waiting_elem) {
                        DEBUG("unexpected character \"%c\" at pos %d", c, pos);
                        ctx_free(ctx);
                        return NULL;
                    }
                }

                waiting_elem = FALSE;

                i = ++pos;
                s[0] = 0;
                sl = 0;
                end_found = FALSE;
                for (i = pos; !end_found && (i < length); i++) {
                    c = input[i];
                    switch (c) {
                        case '\\':
                            /* Escape codes */
                            if (i < length - 1) {
                                c2 = input[i + 1];
                                i++;
                                switch (c2) {
                                    case 'b':
                                        if (sl < JSON_MAX_VALUE_LEN) {
                                            s[sl] = '\b'; s[++sl] = 0;
                                        }

                                        break;

                                    case 'f':
                                        if (sl < JSON_MAX_VALUE_LEN) {
                                            s[sl] = '\f'; s[++sl] = 0;
                                        }

                                        break;

                                    case 'n':
                                        if (sl < JSON_MAX_VALUE_LEN) {
                                            s[sl] = '\n'; s[++sl] = 0;
                                        }

                                        break;

                                    case 'r':
                                        if (sl < JSON_MAX_VALUE_LEN) {
                                            s[sl] = '\r'; s[++sl] = 0;
                                        }

                                        break;

                                    case 't':
                                        if (sl < JSON_MAX_VALUE_LEN) {
                                            s[sl] = '\t'; s[++sl] = 0;
                                        }

                                        break;

                                    case '"':
                                        if (sl < JSON_MAX_VALUE_LEN) {
                                            s[sl] = '"'; s[++sl] = 0;
                                        }

                                        break;

                                    case '\\':
                                        if (sl < JSON_MAX_VALUE_LEN) {
                                            s[sl] = '\\'; s[++sl] = 0;
                                        }

                                        break;

                                    default:
                                        if (sl < JSON_MAX_VALUE_LEN) {
                                            s[sl] = c; s[++sl] = 0;
                                        }
                                }
                            }
                            else { /* The last char in input string is a backslash */
                                if (sl < JSON_MAX_VALUE_LEN) {
                                    s[sl] = '\\'; s[++sl] = 0;
                                }
                            }

                            break;

                        case '"':
                            /* String end */
                            pos = i + 1;
                            end_found = TRUE;
                            break;

                        default:
                            /* Regular character inside string */
                            if (sl < JSON_MAX_VALUE_LEN) {
                                s[sl] = c; s[++sl] = 0;
                            }
                    }
                }

                if (!end_found) {
                    DEBUG("unterminated string at pos %d", pos);
                    ctx_free(ctx);
                    return NULL;
                }

                json = ctx_get_current(ctx);
                if (json) {
                    if (json_get_type(json) == JSON_TYPE_OBJ) {
                        if (ctx_has_key(ctx)) { /* String is a value */
                            ctx_add(ctx, json_str_new(s));
                        }
                        else { /* String is a key */
                            ctx_set_key(ctx, s);
                        }
                    }
                    else if (json_get_type(json) == JSON_TYPE_LIST) {
                        ctx_add(ctx, json_str_new(s));
                    }
                    else {
                        DEBUG("unexpected string at pos %d", pos);
                        ctx_free(ctx);
                        return NULL;
                    }
                }
                else { /* Root element is a string */
                    ctx_add(ctx, json_str_new(s));
                }

                break;

            default:
                /* null, true, false, a number or unexpected character */

                json = ctx_get_current(ctx);
                if ((json && json_get_type(json) == JSON_TYPE_OBJ && !ctx_has_key(ctx)) || !waiting_elem) {
                    DEBUG("unexpected character \"%c\" at pos %d", c, pos);
                    ctx_free(ctx);
                    return NULL;
                }

                waiting_elem = FALSE;

                if ((c >= '0' && c <= '9') || (c == '-')) {
                    /* number */
                    i = pos;
                    if (c == '-') {
                        i++;
                    }

                    point_seen = FALSE; /* One single point allowed in numerals */
                    while (i < length) {
                        c = input[i];
                        if (c == '.') {
                            if (point_seen) {
                                break;
                            }
                            else {
                                point_seen = TRUE;
                            }
                        }
                        else if (c < '0' || c > '9') {
                            break;
                        }

                        i++;
                    }

                    strncpy(s, input + pos, i - pos + 1);
                    s[i - pos + 1] = 0;
                    if (point_seen) { /* floating point */
                        ctx_add(ctx, json_double_new(strtod(s, NULL)));
                    }
                    else { /* integer */
                        ctx_add(ctx, json_int_new(strtol(s, NULL, 10)));
                    }
                    pos = i;
                }
                else if (!strncmp(input + pos, "null", 4)) {
                    ctx_add(ctx, json_null_new());
                    pos += 4;
                }
                else if (!strncmp(input + pos, "false", 5)) {
                    ctx_add(ctx, json_bool_new(FALSE));
                    pos += 5;
                }
                else if (!strncmp(input + pos, "true", 4)) {
                    ctx_add(ctx, json_bool_new(TRUE));
                    pos += 4;
                }
                else {
                    DEBUG("unexpected character \"%c\" at pos %d", c, pos);
                    ctx_free(ctx);
                    return NULL;
                }
        }
    }

    if (!root) {
        if (ctx_get_size(ctx) < 1) {
            DEBUG("empty input");
            ctx_free(ctx);
            return NULL;
        }

        if (ctx_get_size(ctx) > 1) {
            DEBUG("unbalanced brackets");
            ctx_free(ctx);
            return NULL;
        }

        root = ctx_pop(ctx);
        if (json_get_type(root) == JSON_TYPE_LIST || json_get_type(root) == JSON_TYPE_OBJ) {
            /* List and object roots should have already been popped as soon as closing brackets were encountered */
            json_free(root);
            DEBUG("unbalanced brackets");
            ctx_free(ctx);
            return NULL;
        }
    }

    if (json_get_type(root) == JSON_TYPE_OBJ && ctx_has_key(ctx)) {
        DEBUG("expected element at pos %d", pos);
        ctx_free(ctx);
        return NULL;
    }

    ctx_free(ctx);

    return root;
}

char *json_dump(json_t *json, uint8 free_mode) {
    char *output = NULL;
    int len = 0, size = 0;
    
    json_dump_rec(json, &output, &len, &size, free_mode);
    size = realloc_chunks(&output, size, len + 1);
    output[len] = 0;

    return output;
}

char *json_dump_r(json_t *json, uint8 free_mode) {
    int len = 0;
    static char *dump_buffer = NULL;
    static int dump_buffer_size = 0;

    json_dump_rec(json, &dump_buffer, &len, &dump_buffer_size, free_mode);
    dump_buffer_size = realloc_chunks(&dump_buffer, dump_buffer_size, len + 1);
    dump_buffer[len] = 0;

    return dump_buffer;
}

void json_stringify(json_t *json) {
    if (json->type == JSON_TYPE_STRINGIFIED) {
        return; /* Already stringified */
    }

    char *stringified = json_dump_r(json, /* free_mode = */ JSON_FREE_MEMBERS);

    uint16 i = 0, chunks = 0, chunk, pos;
    char c, *s = stringified;

    while ((c = *s++)) {
        chunk = i / STRINGIFIED_CHUNK_SIZE;
        pos = i % STRINGIFIED_CHUNK_SIZE;

        if (chunk >= chunks) {
            json->stringified_data.chunks = realloc(json->stringified_data.chunks, sizeof(char *) * ++chunks);
            json->stringified_data.chunks[chunk] = malloc(STRINGIFIED_CHUNK_SIZE);
        }

        json->stringified_data.chunks[chunk][pos] = c;
        i++;
    }

    json->stringified_data.len = i;
    json->type = JSON_TYPE_STRINGIFIED;
}

void json_free(json_t *json) {
    int i, n, r;

    switch (json->type) {
        case JSON_TYPE_NULL:
        case JSON_TYPE_BOOL:
        case JSON_TYPE_INT:
        case JSON_TYPE_DOUBLE:
        case JSON_TYPE_MEMBERS_FREED:
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

        case JSON_TYPE_STRINGIFIED:
            n = json->stringified_data.len / STRINGIFIED_CHUNK_SIZE;
            r = json->stringified_data.len % STRINGIFIED_CHUNK_SIZE;
            if (r) {
                n++;
            }

            for (i = 0; i < n; i++) {
                free(json->stringified_data.chunks[i]);
            }
            free(json->stringified_data.chunks);

            json->stringified_data.chunks = NULL;
            json->stringified_data.len = 0;

            break;
    }

    free(json);
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
            int i, n, r;

            json_t *new_json = malloc(sizeof(json_t));
            new_json->type = JSON_TYPE_STRINGIFIED;
            new_json->stringified_data.len = json->stringified_data.len;

            n = json->stringified_data.len / STRINGIFIED_CHUNK_SIZE;
            r = json->stringified_data.len % STRINGIFIED_CHUNK_SIZE;
            if (r) {
                n++;
            }

            new_json->stringified_data.chunks = malloc(sizeof(char *) * n);
            for (i = 0; i < n; i++) {
                new_json->stringified_data.chunks[i] = malloc(STRINGIFIED_CHUNK_SIZE);
                memcpy(new_json->stringified_data.chunks[i], json->stringified_data.chunks[i], STRINGIFIED_CHUNK_SIZE);
            }

            return new_json;
        }

        case JSON_TYPE_MEMBERS_FREED:
            DEBUG("cannot duplicate JSON with freed members");
            return NULL;

        default:
            return NULL;
    }
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
    JSON_ASSERT_TYPE(json, JSON_TYPE_LIST);

    json->list_data.children = realloc(json->list_data.children, sizeof(json_t *) * (json->list_data.len + 1));
    json->list_data.children[(int) json->list_data.len++] = child;
}

json_t *json_list_pop_at(json_t *json, uint32 index) {
    json_t *child = json->obj_data.children[index];

    int i;
    for (i = index; i < json->list_data.len - 1; i++) {
        json->list_data.children[i] = json->list_data.children[i + 1];
    }

    json->list_data.len--;
    json->list_data.children = realloc(json->list_data.children, sizeof(json_t *) * json->list_data.len);

    return child;
}

json_t *json_obj_lookup_key(json_t *json, char *key) {
    JSON_ASSERT_TYPE(json, JSON_TYPE_OBJ);

    int i;
    for (i = 0; i < json->obj_data.len; i++) {
        if (!strcmp(key, json->obj_data.keys[i])) {
            return json->obj_data.children[i];
        }
    }

    return NULL;
}

json_t *json_obj_pop_key(json_t *json, char *key) {
    JSON_ASSERT_TYPE(json, JSON_TYPE_OBJ);

    int i, p = -1;
    for (i = 0; i < json->obj_data.len; i++) {
        if (!strcmp(key, json->obj_data.keys[i])) {
            p = i;
            break;
        }
    }

    if (p < 0) {
        return NULL;
    }

    json_t *child = json->obj_data.children[p];
    free(json->obj_data.keys[p]);

    for (i = p; i < json->obj_data.len - 1; i++) {
        json->obj_data.children[i] = json->obj_data.children[i + 1];
        json->obj_data.keys[i] = json->obj_data.keys[i + 1];
    }

    json->obj_data.len--;
    json->obj_data.children = realloc(json->obj_data.children, sizeof(json_t *) * json->obj_data.len);
    json->obj_data.keys = realloc(json->obj_data.keys, sizeof(char *) * json->obj_data.len);

    return child;
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
    JSON_ASSERT_TYPE(json, JSON_TYPE_OBJ);

    json->obj_data.children = realloc(json->obj_data.children, sizeof(json_t *) * (json->obj_data.len + 1));
    json->obj_data.keys = realloc(json->obj_data.keys, sizeof(char *) * (json->obj_data.len + 1));
    json->obj_data.keys[(int) json->obj_data.len] = strdup(key);
    json->obj_data.children[(int) json->obj_data.len++] = child;
}


void json_dump_rec(json_t *json, char **output, int *len, int *size, uint8 free_mode) {
    int i, l, n, r;
    char s[32], *s2, c;
    
    switch (json->type) {
        case JSON_TYPE_NULL:
            *size = realloc_chunks(output, *size, *len + 5);
            strncpy(*output + *len, "null", 5);
            *len += 4;

            break;

        case JSON_TYPE_BOOL:
            if (json_bool_get(json)) {
                *size = realloc_chunks(output, *size, *len + 5);
                strncpy(*output + *len, "true", 5);
                *len += 4;
            }
            else {
                *size = realloc_chunks(output, *size, *len + 6);
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
            s2 = json_str_get(json);
            l = 2; /* For the two quotes */
            while ((c = *s2++)) {
                if (c == '"' || c == '\\' || c == '\b' || c == '\f' || c == '\n' || c == '\r' || c == '\t') {
                    l += 2;
                }
                else {
                    l++;
                }
            }

            *size = realloc_chunks(output, *size, *len + l);
            (*output)[*len] = '"';

            s2 = json_str_get(json);
            i = 1;
            while ((c = *s2++)) {
                switch (c) {
                    case '"':
                        (*output)[*len + i] = '\\';
                        (*output)[*len + i + 1] = '"';
                        i += 2;
                        break;

                    case '\\':
                        (*output)[*len + i] = '\\';
                        (*output)[*len + i + 1] = '\\';
                        i += 2;
                        break;

                    case '\b':
                        (*output)[*len + i] = '\\';
                        (*output)[*len + i + 1] = 'b';
                        i += 2;
                        break;

                    case '\f':
                        (*output)[*len + i] = '\\';
                        (*output)[*len + i + 1] = 'f';
                        i += 2;
                        break;

                    case '\n':
                        (*output)[*len + i] = '\\';
                        (*output)[*len + i + 1] = 'n';
                        i += 2;
                        break;

                    case '\r':
                        (*output)[*len + i] = '\\';
                        (*output)[*len + i + 1] = 'r';
                        i += 2;
                        break;

                    case '\t':
                        (*output)[*len + i] = '\\';
                        (*output)[*len + i + 1] = 't';
                        i += 2;
                        break;

                    default:
                        (*output)[*len + i] = c;
                        i++;
                }
            }

            *len += l;
            (*output)[*len - 1] = '"';

            break;

        case JSON_TYPE_LIST:
            *size = realloc_chunks(output, *size, *len + 1);
            (*output)[(*len)++] = '[';
            for (i = 0; i < json->list_data.len; i++) {
                json_dump_rec(json->list_data.children[i], output, len, size,
                              free_mode >= JSON_FREE_MEMBERS ? JSON_FREE_EVERYTHING : JSON_FREE_NOTHING);
                if (i < json->list_data.len - 1) {
                    *size = realloc_chunks(output, *size, *len + 1);
                    (*output)[(*len)++] = ',';
                }
            }
            *size = realloc_chunks(output, *size, *len + 1);
            (*output)[(*len)++] = ']';

            if (free_mode >= JSON_FREE_MEMBERS) {
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

                json_dump_rec(json->obj_data.children[i], output, len, size,
                              free_mode >= JSON_FREE_MEMBERS ? JSON_FREE_EVERYTHING : JSON_FREE_NOTHING);
                if (i < json->obj_data.len - 1) {
                    *size = realloc_chunks(output, *size, *len + 1);
                    (*output)[(*len)++] = ',';
                }

                if (free_mode >= JSON_FREE_MEMBERS) {
                    free(json->obj_data.keys[i]);
                }
            }
            *size = realloc_chunks(output, *size, *len + 1);
            (*output)[(*len)++] = '}';

            if (free_mode >= JSON_FREE_MEMBERS) {
                json->obj_data.len = 0;
            }

            break;

        case JSON_TYPE_STRINGIFIED:
            n = json->stringified_data.len / STRINGIFIED_CHUNK_SIZE;
            r = json->stringified_data.len % STRINGIFIED_CHUNK_SIZE;

            for (i = 0; i < n; i++) {
                *size = realloc_chunks(output, *size, *len + STRINGIFIED_CHUNK_SIZE);
                memcpy(*output + *len, json->stringified_data.chunks[i], STRINGIFIED_CHUNK_SIZE);
                *len += STRINGIFIED_CHUNK_SIZE;
            }

            if (r) {
                *size = realloc_chunks(output, *size, *len + r);
                memcpy(*output + *len, json->stringified_data.chunks[i], r);
                *len += r;
            }

            break;

        case JSON_TYPE_MEMBERS_FREED:
            DEBUG("cannot dump JSON with freed members");
            break;
    }

    if (free_mode >= JSON_FREE_EVERYTHING) {
        json_free(json);
    }
    else if (free_mode == JSON_FREE_MEMBERS) {
        switch (json->type) {
            case JSON_TYPE_STR:
                free(json->str_value);
                json->str_value = NULL;
                break;

            case JSON_TYPE_LIST:
                free(json->list_data.children);
                json->list_data.children = NULL;
                break;

            case JSON_TYPE_OBJ:
                free(json->obj_data.keys);
                json->obj_data.keys = NULL;
                free(json->obj_data.children);
                json->obj_data.children = NULL;
                break;

            case JSON_TYPE_STRINGIFIED:
                n = json->stringified_data.len / STRINGIFIED_CHUNK_SIZE;
                r = json->stringified_data.len % STRINGIFIED_CHUNK_SIZE;
                if (r) {
                    n++;
                }

                for (i = 0; i < n; i++) {
                    free(json->stringified_data.chunks[i]);
                }
                free(json->stringified_data.chunks);

                json->stringified_data.chunks = NULL;
                json->stringified_data.len = 0;

        }

        json->type = JSON_TYPE_MEMBERS_FREED;
    }
}

ctx_t *ctx_new(char *input) {
    return zalloc(sizeof(ctx_t));
}

void ctx_set_key(ctx_t *ctx, char *key) {
    if (ctx->stack_size < 1) {
        return;
    }

    stack_t *stack_item = ctx->stack + (ctx->stack_size - 1);
    if (stack_item->key) {
        free(stack_item->key);
        stack_item->key = NULL;
    }

    if (key) {
        stack_item->key = strdup(key);
    }
}

void ctx_clear_key(ctx_t *ctx) {
    ctx_set_key(ctx, NULL);
}

json_t *ctx_get_current(ctx_t *ctx) {
    if (ctx->stack_size < 1) {
        return NULL;
    }

    return ctx->stack[ctx->stack_size - 1].json;
}

bool ctx_add(ctx_t *ctx, json_t *json) {
    if (ctx->stack_size < 1) {
        ctx_push(ctx, json);
        return TRUE;
    }

    json_t *current = ctx->stack[ctx->stack_size - 1].json;
    if (current->type == JSON_TYPE_LIST) {
        if (ctx_has_key(ctx)) {
            return FALSE; /* Refuse to add to list if key is set */
        }

        json_list_append(current, json);
    }
    else if (current->type == JSON_TYPE_OBJ) {
        if (!ctx_has_key(ctx)) {
            return FALSE; /* Refuse to add to object if key is unset */
        }

        json_obj_append(current, ctx_get_key(ctx), json);
        ctx_clear_key(ctx);
    }
    else {
        return FALSE; /* Cannot add child to to primitive element */
    }

    return TRUE;
}

void ctx_push(ctx_t *ctx, json_t *json) {
    ctx->stack = realloc(ctx->stack, sizeof(stack_t) * (++ctx->stack_size));
    ctx->stack[ctx->stack_size - 1].json = json;
    ctx->stack[ctx->stack_size - 1].key = NULL;
}

json_t *ctx_pop(ctx_t *ctx) {
    if (ctx->stack_size == 0) {
        return NULL; /* Shouldn't happen */
    }

    if (ctx->stack_size == 1) {
        /* When popping root element, return it */
        ctx->stack_size--;
        json_t *root = ctx->stack[0].json;
        if (ctx->stack[0].key) {
            free(ctx->stack[0].key);
        }
        free(ctx->stack);
        ctx->stack = NULL;

        return root;
    }

    json_t *current = ctx->stack[ctx->stack_size - 1].json;
    ctx->stack = realloc(ctx->stack, sizeof(stack_t) * (--ctx->stack_size));

    /* Add popped element to parent */
    ctx_add(ctx, current);

    /* Return NULL to indicated that popped element has been added to parent */
    return NULL;
}

void ctx_free(ctx_t *ctx) {
    if (ctx->stack_size) {
        /* Will free all JSON elements in hierarchy */
        json_free(ctx->stack[0].json);

        /* Current element is not part of hierarchy */
        if (ctx->stack_size > 1) {
            json_free(ctx->stack[ctx->stack_size - 1].json);
        }

        /* Free keys */
        int i;
        for (i = 0; i < ctx->stack_size; i++) {
            if (ctx->stack[i].key) {
                free(ctx->stack[i].key);
            }
        }

        free(ctx->stack);
        ctx->stack_size = 0;
        ctx->stack = NULL;
    }

    free(ctx);
}
