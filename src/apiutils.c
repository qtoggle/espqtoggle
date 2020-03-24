
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
#include <stdarg.h>
#include <mem.h>

#include "espgoodies/common.h"
#include "espgoodies/utils.h"
#include "espgoodies/wifi.h"
#ifdef _SLEEP
#include "espgoodies/sleep.h"
#endif

#include "api.h"
#include "common.h"
#include "ports.h"
#include "apiutils.h"


double get_choice_value_num(char *choice) {
    return strtod(get_choice_value_str(choice), NULL);
}

char *get_choice_value_str(char *choice) {
    static char *value = NULL;  /* acts as a reentrant buffer */
    if (value) {
        free(value);
        value = NULL;
    }

    char *p = strchr(choice, ':');
    if (p) {
        value = strndup(choice, p - choice);
    }
    else {
        return choice;
    }

    return value;
}

char *get_choice_display_name(char *choice) {
    static char *display_name = NULL;  /* acts as a reentrant buffer */
    if (display_name) {
        free(display_name);
        display_name = NULL;
    }

    char *p = strchr(choice, ':');
    if (p) {
        display_name = strdup(p + 1);
    }
    else {
        return NULL;
    }

    return display_name;
}

json_t *choice_to_json(char *choice, char type) {
    json_t *choice_json = json_obj_new();

    char *display_name = get_choice_display_name(choice);
    if (display_name) {
        json_obj_append(choice_json, "display_name", json_str_new(display_name));
    }

    if (type == ATTR_TYPE_NUMBER) /* also PORT_TYPE_NUMBER */ {
        json_obj_append(choice_json, "value", json_double_new(get_choice_value_num(choice)));
    }
    else {
        json_obj_append(choice_json, "value", json_str_new(get_choice_value_str(choice)));
    }

    return choice_json;
}

void free_choices(char **choices) {
    char *c;
    while ((c = *choices++)) {
        free(c);
    }

    free(choices);
}

bool choices_equal(char **choices1, char **choices2) {
    if (choices1 == choices2) {
        return TRUE;
    }

    /* make sure both lists are available */
    if (!choices1 || !choices2) {
        return FALSE;
    }

    char *c1, *c2;
    while ((c1 = *choices1++)) {
        c2 = *choices2++;
        if (!c2) {
            return FALSE;
        }

        if (strcmp(c1, c2)) {
            return FALSE;
        }
    }

    return TRUE;
}

bool validate_num(double value, double min, double max, bool integer, double step, char **choices) {
    if ((min != max) && ((!IS_UNDEFINED(min) && value < min) || (!IS_UNDEFINED(max) && value > max))) {
        return FALSE;
    }

    if (choices) {
        char *c;
        int i = 0;
        while ((c = *choices++)) {
            if (get_choice_value_num(c) == value) {
                return i + 1;
            }

            i++;
        }

        return FALSE;
    }

    if (integer) {
        if (ceil(value) != value) {
            return FALSE;
        }

        if (step && !IS_UNDEFINED(step)) {
            double r = (value - min) / step;
            if (ceil(r) != r) {
                return FALSE;
            }
        }
    }

    return TRUE;
}

bool validate_str(char *value, char **choices) {
    if (choices) {
        char *c;
        int i = 0;
        while ((c = *choices++)) {
            if (!strcmp(get_choice_value_str(c), value)) {
                return i + 1;
            }

            i++;
        }

        return FALSE;
    }

    return TRUE;
}

bool validate_id(char *id) {
    if (!isalpha((int) id[0]) && id[0] != '_') {
        return FALSE;
    }

    int c;
    while ((c = *id++)) {
        if (!isalnum(c) && c != '_' && c != '-') {
            return FALSE;
        }
    }

    return TRUE;
}

bool validate_ip_address(char *ip, uint8 *a) {
    a[0] = 0;
    char *s = ip;
    int c, i = 0;
    while ((c = *s++)) {
        if (isdigit(c)) {
            a[i] = a[i] * 10 + (c - '0');
        }
        else if ((c == '.') && (i < 3)) {
            i++;
            a[i] = 0;
        }
        else {
            return FALSE;
        }
    }

    if (i < 3) {
        return FALSE;
    }

    return TRUE;
}

bool validate_wifi_ssid(char *ssid) {
    int len = strlen(ssid);
    return len > 0 && len <= WIFI_SSID_MAX_LEN;
}

bool validate_wifi_key(char *key) {
    return strlen(key) <= WIFI_PSK_MAX_LEN;
}

bool validate_wifi_bssid(char *bssid_str, uint8 *bssid) {
    int len = 0;
    char *s = bssid_str;
    char t[3] = {0, 0, 0};

    while (TRUE) {
        if (len > WIFI_BSSID_LEN) {
            return FALSE;
        }

        if (!s[0]) {
            break;
        }

        if (!s[1]) {
            return FALSE;
        }

        t[0] = s[0]; t[1] = s[1];
        bssid[len++] = strtol(t, NULL, 16);

        s += 3;
    }

    if (len != WIFI_BSSID_LEN && len != 0) {
        return FALSE;
    }

    return TRUE;
}


bool validate_str_network_scan(char *scan, int *scan_interval, int *scan_threshold) {
    char c, *s = scan;
    int pos = 0;
    int no = 0;
    while ((c = *s++)) {
        if (c == ':') {
            if (pos >= 1) {
                return FALSE;
            }

            pos++;
            *scan_interval = no;
            no = 0;
        }
        else if (isdigit((int) c)) {
            no = no * 10 + c - '0';
        }
        else {
            return FALSE;
        }
    }

    if (pos == 1) {
        *scan_threshold = no;
    }
    else {
        return FALSE;
    }

    return TRUE;
}

#ifdef _SLEEP

bool validate_str_sleep_mode(char *sleep_mode, int *wake_interval, int *wake_duration) {
    char *s = sleep_mode;
    int c, wi = 0, wd = 0;

    while ((c = *s++)) {
        if (c == ':') {
            break;
        }
        else if (isdigit(c)) {
            wi = wi * 10 + (c - '0');
        }
        else {
            return FALSE;
        }
    }

    if (!c) {
        return FALSE;
    }

    while ((c = *s++)) {
        if (isdigit(c)) {
            wd = wd * 10 + (c - '0');
        }
        else {
            return FALSE;
        }
    }

    if (wi < SLEEP_WAKE_INTERVAL_MIN || wi > SLEEP_WAKE_INTERVAL_MAX) {
        return FALSE;
    }

    if (wd < SLEEP_WAKE_DURATION_MIN || wd > SLEEP_WAKE_DURATION_MAX) {
        return FALSE;
    }

    *wake_interval = wi;
    *wake_duration = wd;

    return TRUE;
}

#endif

json_t *attrdef_to_json(char *display_name, char *description, char *unit, char type, bool modifiable,
                        double min, double max, bool integer, double step, char **choices,
                        bool reconnect) {

    json_t *json = json_obj_new();

    json_obj_append(json, "display_name", json_str_new(display_name));
    json_obj_append(json, "description", json_str_new(description));
    if (unit) {
        json_obj_append(json, "unit", json_str_new(unit));
    }
    json_obj_append(json, "modifiable", json_bool_new(modifiable));

    switch (type) {
        case ATTR_TYPE_BOOLEAN:
            json_obj_append(json, "type", json_str_new(API_ATTR_TYPE_BOOLEAN));
            break;

        case ATTR_TYPE_NUMBER:
            json_obj_append(json, "type", json_str_new(API_ATTR_TYPE_NUMBER));
            break;

        case ATTR_TYPE_STRING:
            json_obj_append(json, "type", json_str_new(API_ATTR_TYPE_STRING));
            break;
    }

    if (type == ATTR_TYPE_NUMBER) {
        if (!IS_UNDEFINED(min)) {
            json_obj_append(json, "min", json_double_new(min));
        }

        if (!IS_UNDEFINED(max)) {
            json_obj_append(json, "max", json_double_new(max));
        }

        if (integer) {
            json_obj_append(json, "integer", json_bool_new(TRUE));
        }

        if (step && !IS_UNDEFINED(step)) {
            json_obj_append(json, "step", json_double_new(step));
        }
    }

    if (choices) {
        json_t *list = json_list_new();
        char *c;
        while ((c = *choices++)) {
            json_list_append(list, choice_to_json(c, type));
        }

        json_obj_append(json, "choices", list);
    }

    if (reconnect) {
        json_obj_append(json, "reconnect", json_bool_new(TRUE));
    }

    return json;
}
