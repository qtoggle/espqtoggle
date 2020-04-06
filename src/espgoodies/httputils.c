
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
#include <mem.h>

#include "common.h"
#include "utils.h"
#include "httputils.h"


ICACHE_FLASH_ATTR static void       unescape_url_encoded_value(char *value);


json_t *http_parse_url_encoded(char *input) {
    char name[JSON_MAX_NAME_LEN + 1];
    char value[JSON_MAX_VALUE_LEN + 1];
    name[0] = 0;
    value[0] = 0;

    json_t *json = json_obj_new();

    if (!input) {
        return json;
    }

    char *s = input;
    int c, state = 0;
    while ((c = *s++)) {
        switch (state) {
            case 0: { /* Name */
                if (c == '=') { /* Value starts */
                    state = 1; /* Value */
                }
                else if (c == '&') { /* Field starts */
                    json_obj_append(json, name, json_str_new(value));

                    name[0] = 0;
                    value[0] = 0;
                }
                else {
                    append_max_len(name, c, JSON_MAX_NAME_LEN);
                }

                break;
            }

            case 1: { /* Value */
                if (c == '&') { /* Field starts */
                    unescape_url_encoded_value(value);

                    json_obj_append(json, name, json_str_new(value));

                    state = 0; /* Name */
                    name[0] = 0;
                    value[0] = 0;
                }
                else {
                    append_max_len(value, c, JSON_MAX_VALUE_LIST_LEN);
                }

                break;
            }
        }
    }

    /* Append the last value that hasn't yet been processed, if any */
    if (*name) {
        json_obj_append(json, name, json_str_new(value));
    }

    return json;
}

char *http_build_auth_token_header(char *token) {
    int len = 8 + strlen(token); /* Len("Bearer" + " " + token) */
    char *header = malloc(len);
    snprintf(header, len, "Bearer %s", token);

    return header;
}

char *http_parse_auth_token_header(char *header) {
    /* Look for the first space */
    char *p = header;
    while (*p && *p != ' ') {
        p++;
    }

    if (!*p) {
        /* No space found */
        return NULL;
    }

    if (strncasecmp(header, "Bearer", p - header)) {
        /* Header does not start with "Bearer" */
        return NULL;
    }

    /* Skip all spaces */
    while (*p && *p == ' ') {
        p++;
    }

    if (!*p) {
        /* No token present */
        return NULL;
    }

    /* Everything that's left is token */
    return strdup(p);
}


void unescape_url_encoded_value(char *value) {
    char *s = value, *unescaped = malloc(strlen(value) + 1);
    char hex[3] = {0, 0, 0};
    int c, p = 0, state = 0;
    
    unescaped[0] = 0;
    
    while ((c = *s)) {
        switch (state) {
            case 0: /* Outside */
                if (c == '%') {
                    state = 1; /* Percent */
                }
                else {
                    unescaped[p] = c;
                    unescaped[++p] = 0;
                }

                break;

            case 1: /* Percent */
                if (c == '%') { /* Escaped percent */
                    state = 0; /* Outside */
                    unescaped[p] = c;
                    unescaped[++p] = 0;
                }
                else if (IS_HEX(c)) {
                    state = 2; /* Hex */
                    hex[0] = c;
                }
                else {
                    state = 0; /* Outside */
                    unescaped[p] = c;
                    unescaped[++p] = 0;
                }

                break;

            case 2: /* Hex */
                if (IS_HEX(c)) {
                    hex[1] = c;
                    
                    c = strtol(hex, NULL, 16);
                    unescaped[p] = c;
                    unescaped[++p] = 0;
                }
                else {
                    unescaped[p] = c;
                    unescaped[++p] = 0;
                }

                state = 0; /* Outside */

                break;
        }
        
        s++;
    }
    
    strcpy(value, unescaped);
    free(unescaped);
}
