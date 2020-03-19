
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

#ifndef _EXPR_H
#define _EXPR_H


#include <c_types.h>


#ifdef _DEBUG_EXPR
#define DEBUG_EXPR(fmt, ...)    DEBUG("[expressions   ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_EXPR(...)         {}
#endif


typedef struct expr {

    double          value;          /* used for constant expressions, value change detection and caching purposes */

    int64           aux;            /* auxiliary flag */
    void          * paux;           /* auxiliary pointer */

    char          * port_id;
    uint16          len;            /* used for value history queue size */

    void          * func;
    int8            argc;
    struct expr  ** args;

} expr_t;

typedef struct {

    char *          name;
    double          value;

} const_t;

struct port;

typedef double (* func_callback_t)(expr_t *expr, int argc, double *args);

typedef struct {

    char          * name;
    int8            argc;           /* if negative, acts as a minimum */
    func_callback_t callback;

} func_t;


ICACHE_FLASH_ATTR expr_t          * expr_parse(char *port_id, char *input, int len);
ICACHE_FLASH_ATTR double            expr_eval(expr_t *expr);
ICACHE_FLASH_ATTR void              expr_free(expr_t *expr);
ICACHE_FLASH_ATTR int               expr_check_loops(expr_t *expr, struct port *the_port);
ICACHE_FLASH_ATTR struct port    ** expr_port_deps(expr_t *expr);
ICACHE_FLASH_ATTR bool              expr_is_time_dep(expr_t *expr);
ICACHE_FLASH_ATTR bool              expr_is_time_ms_dep(expr_t *expr);
ICACHE_FLASH_ATTR bool              expr_is_rounding(expr_t *expr);


#endif /* _EXPR_H */

