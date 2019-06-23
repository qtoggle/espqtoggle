
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
            case 0: { /* name */
                if (c == '=') { /* value starts */
                    state = 1; /* value */
                }
                else if (c == '&') { /* field starts */
                    json_obj_append(json, name, json_str_new(value));

                    name[0] = 0;
                    value[0] = 0;
                }
                else {
                    append_max_len(name, c, JSON_MAX_NAME_LEN);
                }

                break;
            }

            case 1: { /* value */
                if (c == '&') { /* field starts */
                    unescape_url_encoded_value(value);

                    json_obj_append(json, name, json_str_new(value));

                    state = 0; /* name */
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

    /* append the last value that hasn't yet been processed, if any */
    if (*name) {
        json_obj_append(json, name, json_str_new(value));
    }

    return json;
}

char *http_build_auth_token_header(char *token) {
    int len = 8 + strlen(token); /* len("Bearer" + " " + token) */
    char *header = malloc(len);
    snprintf(header, len, "Bearer %s", token);

    return header;
}

char *http_parse_auth_token_header(char *header) {
    /* look for the first space */
    char *p = header;
    while (*p && *p != ' ') {
        p++;
    }

    if (!*p) {
        /* no space found */
        return NULL;
    }

    if (strncasecmp(header, "Bearer", p - header)) {
        /* header does not start with "Bearer" */
        return NULL;
    }

    /* skip all spaces */
    while (*p && *p == ' ') {
        p++;
    }

    if (!*p) {
        /* no token present */
        return NULL;
    }

    /* everything that's left is token */
    return strdup(p);
}


void unescape_url_encoded_value(char *value) {
    char *s = value, *unescaped = malloc(strlen(value) + 1);
    char hex[3] = {0, 0, 0};
    int c, p = 0, state = 0;
    
    unescaped[0] = 0;
    
    while ((c = *s)) {
        switch (state) {
            case 0: /* outside */
                if (c == '%') {
                    state = 1; /* percent */
                }
                else {
                    unescaped[p] = c;
                    unescaped[++p] = 0;
                }

                break;

            case 1: /* percent */
                if (c == '%') { /* escaped percent */
                    state = 0; /* outside */
                    unescaped[p] = c;
                    unescaped[++p] = 0;
                }
                else if (IS_HEX(c)) {
                    state = 2; /* hex */
                    hex[0] = c;
                }
                else {
                    state = 0; /* outside */
                    unescaped[p] = c;
                    unescaped[++p] = 0;
                }

                break;

            case 2: /* hex */
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

                state = 0; /* outside */

                break;
        }
        
        s++;
    }
    
    strcpy(value, unescaped);
    free(unescaped);
}
