
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

#ifndef _EXPR_H
#define _EXPR_H


#ifdef _DEBUG_EXPR
#define DEBUG_EXPR(fmt, ...)    DEBUG("[expressions   ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_EXPR(...)         {}
#endif

#define EXPR_TIME_DEP_BIT       30  /* used in port->change_dep_mask */
#define EXPR_TIME_MS_DEP_BIT    29  /* used in port->change_dep_mask */


typedef struct expr {

    double          value;          /* used for constant expressions and other caching purposes */
    double          prev_value;     /* used to determine value changes */
    int             aux;            /* auxiliary flag */

    union {
        char      * port_id;
        uint16      len;            /* used for value history queue size */
    };

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


ICACHE_FLASH_ATTR expr_t          * expr_parse(char *input, int len);
ICACHE_FLASH_ATTR double            expr_eval(expr_t *expr);
ICACHE_FLASH_ATTR void              expr_free(expr_t *expr);
ICACHE_FLASH_ATTR int               expr_check_loops(expr_t *expr, struct port *the_port);
ICACHE_FLASH_ATTR struct port    ** expr_port_deps(expr_t *expr);
ICACHE_FLASH_ATTR bool              expr_is_time_dep(expr_t *expr);
ICACHE_FLASH_ATTR bool              expr_is_time_ms_dep(expr_t *expr);
ICACHE_FLASH_ATTR bool              expr_is_rounding(expr_t *expr);


#endif /* _EXPR_H */

