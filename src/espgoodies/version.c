
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

#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "espgoodies/common.h"
#include "espgoodies/version.h"


static char *types_str[] = {
    "", /* stable */
    "alpha",
    "beta",
    "rc",
    "",
    "",
    "",
    "unknown"
};


void version_parse(char *version_str, version_t *version) {
    char c;
    char type_str[64] = {0};
    uint8 len;
    uint8 parts[4] = {0, 0, 0, 0};
    uint8 part_index = 0;
    while ((c = *version_str++)) {
        if (isdigit((int) c)) {
            parts[part_index] *= 10;
            parts[part_index] += c - '0';
        }
        else if (isalpha((int) c)) {
            len = strlen(type_str);
            if (len < sizeof(type_str) - 1) {
                type_str[len] = c;
                type_str[len + 1] = '\0';
            }
        }
        else if (c == '.') {
            if (part_index < 3) {
                part_index++;
            }
        }
    }

    version->major = parts[0];
    version->minor = parts[1];
    version->patch = parts[2];
    version->label = parts[3];

    for (int i = 0; i <= VERSION_TYPE_UNKNOWN; i++) {
        if (!strcmp(type_str, types_str[i])) {
            version->type = i;
            break;
        }
    }
}

char *version_dump(version_t *version) {
    char *version_str = malloc(24);

    char *type_str = types_str[version->type];
    if (type_str[0]) {
        snprintf(
            version_str,
            24,
            "%d.%d.%d-%s.%d",
            version->major,
            version->minor,
            version->patch,
            types_str[version->type],
            version->label
        );
    }
    else {
        snprintf(version_str, 24, "%d.%d.%d", version->major, version->minor, version->patch);
    }
    version_str[23] = '\0';

    return version_str;
}

void version_from_int(version_t *version, uint32 version_int) {
    version->major = (version_int >> 24) & 0xFF;
    version->minor = (version_int >> 16) & 0xFF;
    version->patch = (version_int >> 8) & 0xFF;
    version->label = (version_int & 0xFF) >> 3; /* First 5 bits of last byte represent the label */
    version->type = version_int & 0x07; /* Last 3 bits of last byte represent the version type */
}

uint32 version_to_int(version_t *version) {
    return (version->major << 24) |
           (version->minor << 16) |
           (version->patch << 8) |
           (version->label << 3) |
            version->type;
}

char *version_type_str(uint8 type) {
    return types_str[type];
}

int version_cmp(version_t *version1, version_t *version2) {
    uint32 version1_int = version_to_int(version1);
    uint32 version2_int = version_to_int(version2);
    if (version1_int < version2_int) {
        return -1;
    }
    if (version1_int > version2_int) {
        return 1;
    }

    return 0;
}
