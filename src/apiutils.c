
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

#include <mem.h>

#include "espgoodies/common.h"
#include "espgoodies/utils.h"

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
