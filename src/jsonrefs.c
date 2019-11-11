
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

#include <stdarg.h>
#include <mem.h>

#include "espgoodies/common.h"
#include "espgoodies/json.h"

#include "apiutils.h"
#include "jsonrefs.h"


json_t *make_json_ref(const char *target_fmt, ...) {
    va_list args;
    char *buf = malloc(256);

    va_start(args, target_fmt);
    vsnprintf(buf, 256, target_fmt, args);
    va_end(args);

    json_t *ref = json_obj_new();
    json_obj_append(ref, "$ref", json_str_new(buf));
    free(buf);

    return ref;
}

void lookup_port_attrdef_choices(char **choices, port_t *port, attrdef_t *attrdef, int8 *found_port_index,
                                 char **found_attrdef_name, json_refs_ctx_t *json_refs_ctx) {

    *found_port_index = -1;
    *found_attrdef_name = NULL;

    port_t *p, **ports = all_ports;
    attrdef_t *a, **attrdefs;
    uint8 pi = 0;

    if (json_refs_ctx->type == JSON_REFS_TYPE_PORTS_LIST) {
        /* look through the attrdefs of all ports for similar choices */
        while ((p = *ports++)) {
            if ((attrdefs = p->attrdefs)) {
                while ((a = *attrdefs++) && (a != attrdef)) {
                    if (choices_equal(choices, a->choices)) {
                        *found_port_index = pi;
                        *found_attrdef_name = a->name;
                        break;
                    }
                }
            }

            pi++;
            if (p == port) {
                break;
            }
        }
    }
    else {
        /* look through the attrdefs of this port for similar choices */
        if ((attrdefs = port->attrdefs)) {
            while ((a = *attrdefs++) && (a != attrdef)) {
                if (choices_equal(choices, a->choices)) {
                    *found_attrdef_name = a->name;
                    break;
                }
            }
        }
    }
}

void json_refs_ctx_init(json_refs_ctx_t *json_refs_ctx, uint8 type) {
    json_refs_ctx->type = type;
    json_refs_ctx->index = 0;

    json_refs_ctx->filter_port_index = -1;
    json_refs_ctx->filter_width_port_index = -1;
    json_refs_ctx->debounce_port_index = -1;
    json_refs_ctx->sampling_interval_port_index = -1;
}
