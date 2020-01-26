
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
#include <limits.h>
#include <ctype.h>
#include <mem.h>

#include "espgoodies/common.h"
#include "espgoodies/system.h"
#include "espgoodies/utils.h"

#include "common.h"
#include "ports.h"
#include "expr.h"


#define MAX_NAME_LEN    16
#define MAX_ARGS        16
#define MAX_HIST_LEN    16

#define isidornum(c) (isalnum(c) || c == '.' || c == '_' || c == '$' || c == '-' || c == '+')

typedef struct {

    double      value;
    int         time_ms;

} value_hist_t;

ICACHE_FLASH_ATTR static double     _add_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _sub_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _mul_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _div_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _mod_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _and_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _or_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _not_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _xor_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _bitand_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _bitor_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _bitnot_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _bitxor_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _shl_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _shr_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _if_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _eq_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _gt_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _gte_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _lt_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _lte_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _abs_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _sgn_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _min_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _max_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _floor_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _ceil_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _round_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _time_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _timems_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _held_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _delay_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _hyst_callback(expr_t *expr, int argc, double *args);

ICACHE_FLASH_ATTR static const_t *  find_const_by_name(char *name);
ICACHE_FLASH_ATTR static func_t *   find_func_by_name(char *name);
ICACHE_FLASH_ATTR static expr_t *   parse_port_id_expr(char *port_id, char *input);
ICACHE_FLASH_ATTR static expr_t *   parse_const_expr(char *input);
ICACHE_FLASH_ATTR static int        check_loops_rec(port_t *the_port, int level, expr_t *expr);


const_t _false = {.name = "false", .value = 0};
const_t _true =  {.name = "true",  .value = 1};

const_t *constants[] = {
    &_false,
    &_true,
    NULL
};


double _add_callback(expr_t *expr, int argc, double *args) {
    int i;
    double s = 0;
    for (i = 0; i < argc; i++) {
        s += args[i];
    }
    
    return s;
}

double _sub_callback(expr_t *expr, int argc, double *args) {
    return args[0] - args[1];
}

double _mul_callback(expr_t *expr, int argc, double *args) {
    int i;
    double p = 1;
    for (i = 0; i < argc; i++) {
        p *= args[i];
    }

    return p;
}

double _div_callback(expr_t *expr, int argc, double *args) {
    return args[0] / args[1];
}

double _mod_callback(expr_t *expr, int argc, double *args) {
    return (int) args[0] % (int) args[1];
}

double _and_callback(expr_t *expr, int argc, double *args) {
    int i;
    bool r = TRUE;
    for (i = 0; i < argc; i++) {
        r = r && args[i];
    }

    return r;
}

double _or_callback(expr_t *expr, int argc, double *args) {
    int i;
    bool r = FALSE;
    for (i = 0; i < argc; i++) {
        r = r || args[i];
    }

    return r;
}

double _not_callback(expr_t *expr, int argc, double *args) {
    return !args[0];
}

double _xor_callback(expr_t *expr, int argc, double *args) {
    return (args[0] && !args[1]) || (!args[0] && args[1]);
}

double _bitand_callback(expr_t *expr, int argc, double *args) {
    return (int) args[0] & (int) args[1];
}

double _bitor_callback(expr_t *expr, int argc, double *args) {
    return (int) args[0] | (int) args[1];
}

double _bitnot_callback(expr_t *expr, int argc, double *args) {
    return ~(int) args[0];
}

double _bitxor_callback(expr_t *expr, int argc, double *args) {
    return (int) args[0] ^ (int) args[1];
}

double _shl_callback(expr_t *expr, int argc, double *args) {
    return (int) args[0] << (int) args[1];
}

double _shr_callback(expr_t *expr, int argc, double *args) {
    return (int) args[0] >> (int) args[1];
}

double _if_callback(expr_t *expr, int argc, double *args) {
    return args[0] ? args[1] : args[2];
}

double _eq_callback(expr_t *expr, int argc, double *args) {
    return args[0] == args[1];
}

double _gt_callback(expr_t *expr, int argc, double *args) {
    return args[0] > args[1];
}

double _gte_callback(expr_t *expr, int argc, double *args) {
    return args[0] >= args[1];
}

double _lt_callback(expr_t *expr, int argc, double *args) {
    return args[0] < args[1];
}

double _lte_callback(expr_t *expr, int argc, double *args) {
    return args[0] <= args[1];
}

double _abs_callback(expr_t *expr, int argc, double *args) {
    return abs(args[0]);
}

double _sgn_callback(expr_t *expr, int argc, double *args) {
    return args[0] > 0 ? 1 : args[0] < 0 ? -1 : 0;
}

double _min_callback(expr_t *expr, int argc, double *args) {
    int i;
    double m = INT_MAX;
    for (i = 0; i < argc; i++) {
        if (args[i] < m) {
            m = args[i];
        }
    }
    
    return m;
}

double _max_callback(expr_t *expr, int argc, double *args) {
    int i;
    double m = -INT_MAX;
    for (i = 0; i < argc; i++) {
        if (args[i] > m) {
            m = args[i];
        }
    }
    
    return m;
}

double _floor_callback(expr_t *expr, int argc, double *args) {
    double v = (int) floor(args[0]);

    return v;
}

double _ceil_callback(expr_t *expr, int argc, double *args) {
    return (int) ceil(args[0]);
}

double _round_callback(expr_t *expr, int argc, double *args) {
    double value = args[0];
    int i;

    if (argc > 1) {
        for (i = 0; i < args[1]; i++) {
            value *= 10;
        }
    }

    value = round(value);

    if (argc > 1) {
        for (i = 0; i < args[1]; i++) {
            value /= 10;
        }
    }

    return value;
}

double _time_callback(expr_t *expr, int argc, double *args) {
    return system_uptime();
}

double _timems_callback(expr_t *expr, int argc, double *args) {
    return system_uptime_us() / 1000;
}

double _held_callback(expr_t *expr, int argc, double *args) {
    int time_ms = (int) (system_uptime_us() / 1000);
    double value = args[0];
    double fixed_value = args[1];
    int duration = args[2];
    bool result = FALSE;

    if (!expr->aux) { /* very first expression eval call */
        /* aux flag is used as time counter, in milliseconds */
        expr->aux = time_ms;
    }
    else {
        int delta = time_ms - expr->aux;

        if ((expr->value != value) /* value changed */ || (delta < 0) /* system time overflow */) {
            /* reset held timer */
            expr->aux = time_ms;
        }
        else {
            result = (delta >= duration) && (value == fixed_value);
        }
    }

    expr->value = value;

    return result;
}

double _delay_callback(expr_t *expr, int argc, double *args) {
    int time_ms = (int) (system_uptime_us() / 1000);
    double value = args[0];
    double delay = args[1];
    int i;

    value_hist_t *hist = (void *) expr->aux;

    /* expr->aux flag is used as a pointer to a value history queue */
    /* expr->value is used as current value */

    if (IS_UNDEFINED(expr->value)) { /* very first expression eval call */
        expr->value = value;
    }

    /* detect system time overflow and reset history */
    if (expr->len && hist[0].time_ms > time_ms) {
        expr->len = 0;
        free((void *) expr->aux);
        expr->aux = 0;

        return expr->value;
    }

    /* detect value transitions and build history */
    if (value != expr->prev_value) {
        expr->prev_value = value;

        /* drop elements from queue if history max size reached */
        if (expr->len == MAX_HIST_LEN) {
            for (i = 0; i < expr->len - 1; i++) {
                hist[i] = hist[i + 1];
            }
        }
        else {
            expr->len++;
            expr->aux = (int) (hist = realloc((void *) expr->aux, sizeof(value_hist_t) * expr->len));
        }

        hist[expr->len - 1].value = value;
        hist[expr->len - 1].time_ms = time_ms;
    }

    /* process history */
    while (expr->len && (time_ms - hist[0].time_ms >= delay)) {
        expr->value = hist[0].value;
        for (i = 0; i < expr->len - 1; i++) {
            hist[i] = hist[i + 1];
        }
        expr->len--;
    }

    if (expr->len) {
        expr->aux = (int) realloc((void *) expr->aux, sizeof(value_hist_t) * expr->len);
    }
    else {
        free((void *) expr->aux);
        expr->aux = 0;
    }

    return expr->value;
}

double _hyst_callback(expr_t *expr, int argc, double *args) {
    double value = args[0];
    double threshold1 = args[1];
    double threshold2 = args[2];

    /* expr->value is used as last value */

    if (IS_UNDEFINED(expr->value)) { /* very first expression eval call */
        expr->value = 0;
    }

    expr->value = ((expr->value == 0 && value > threshold1) ||
                   (expr->value != 0 && value >= threshold2));

    return expr->value;
}


func_t _add =    {.name = "ADD",    .argc = -2, .callback = _add_callback};
func_t _sub =    {.name = "SUB",    .argc = 2,  .callback = _sub_callback};
func_t _mul =    {.name = "MUL",    .argc = -2, .callback = _mul_callback};
func_t _div =    {.name = "DIV",    .argc = 2,  .callback = _div_callback};
func_t _mod =    {.name = "MOD",    .argc = 2,  .callback = _mod_callback};
func_t _and =    {.name = "AND",    .argc = -2, .callback = _and_callback};
func_t _or =     {.name = "OR",     .argc = -2, .callback = _or_callback};
func_t _not =    {.name = "NOT",    .argc = 1,  .callback = _not_callback};
func_t _xor =    {.name = "XOR",    .argc = 2,  .callback = _xor_callback};
func_t _bitand = {.name = "BITAND", .argc = 2,  .callback = _bitand_callback};
func_t _bitor =  {.name = "BITOR",  .argc = 2,  .callback = _bitor_callback};
func_t _bitnot = {.name = "BITNOT", .argc = 1,  .callback = _bitnot_callback};
func_t _bitxor = {.name = "BITXOR", .argc = 2,  .callback = _bitxor_callback};
func_t _shl =    {.name = "SHL",    .argc = 2,  .callback = _shl_callback};
func_t _shr =    {.name = "SHR",    .argc = 2,  .callback = _shr_callback};
func_t _if =     {.name = "IF",     .argc = 3,  .callback = _if_callback};
func_t _eq =     {.name = "EQ",     .argc = 2,  .callback = _eq_callback};
func_t _gt =     {.name = "GT",     .argc = 2,  .callback = _gt_callback};
func_t _gte =    {.name = "GTE",    .argc = 2,  .callback = _gte_callback};
func_t _lt =     {.name = "LT",     .argc = 2,  .callback = _lt_callback};
func_t _lte =    {.name = "LTE",    .argc = 2,  .callback = _lte_callback};
func_t _abs =    {.name = "ABS",    .argc = 1,  .callback = _abs_callback};
func_t _sgn =    {.name = "SGN",    .argc = 1,  .callback = _sgn_callback};
func_t _min =    {.name = "MIN",    .argc = -2, .callback = _min_callback};
func_t _max =    {.name = "MAX",    .argc = -2, .callback = _max_callback};
func_t _floor =  {.name = "FLOOR",  .argc = 1,  .callback = _floor_callback};
func_t _ceil =   {.name = "CEIL",   .argc = 1,  .callback = _ceil_callback};
func_t _round =  {.name = "ROUND",  .argc = -1, .callback = _round_callback};
func_t _time =   {.name = "TIME",   .argc = 0,  .callback = _time_callback};
func_t _timems = {.name = "TIMEMS", .argc = 0,  .callback = _timems_callback};
func_t _held =   {.name = "HELD",   .argc = 3,  .callback = _held_callback};
func_t _delay =  {.name = "DELAY",  .argc = 2,  .callback = _delay_callback};
func_t _hyst =   {.name = "HYST",   .argc = 3,  .callback = _hyst_callback};

func_t *funcs[] = {
    &_add,
    &_sub,
    &_mul,
    &_div,
    &_mod,
    &_and,
    &_or,
    &_not,
    &_xor,
    &_bitand,
    &_bitor,
    &_bitnot,
    &_bitxor,
    &_shl,
    &_shr,
    &_if,
    &_eq,
    &_gt,
    &_gte,
    &_lt,
    &_lte,
    &_abs,
    &_sgn,
    &_min,
    &_max,
    &_floor,
    &_ceil,
    &_round,
    &_time,
    &_timems,
    &_held,
    &_delay,
    &_hyst,
    NULL
};


const_t *find_const_by_name(char *name) {
    const_t **constant = constants, *c;
    while ((c = *constant++)) {
        if (!strcmp(c->name, name)) {
            return c;
        }
    }

    return NULL;    
}

func_t *find_func_by_name(char *name) {
    func_t **func = funcs, *f;
    while ((f = *func++)) {
        if (!strcmp(f->name, name)) {
            return f;
        }
    }

    return NULL;    
}


expr_t *parse_port_id_expr(char *port_id, char *input) {
    int c;
    char *s = input;
    
    if (input[0] && !isalpha((int) input[0]) && input[0] != '_') {
        DEBUG_EXPR("invalid port identifier %s", input);
        return NULL;
    }

    while ((c = *s++)) {
        if (!isalnum((int) c) && c != '_' && c != '-') {
            DEBUG_EXPR("invalid port identifier %s", input);
            return NULL;
        }
    }
    
    expr_t *expr = zalloc(sizeof(expr_t));
    expr->value = UNDEFINED;
    expr->prev_value = UNDEFINED;
    
    if (*input) {
        expr->port_id = strdup(input);
    }
    else { /* reference to port itself */
        expr->port_id = port_id;
    }

    return expr;
}

expr_t *parse_const_expr(char *input) {
    const_t *constant = find_const_by_name(input);
    if (constant) {
        expr_t *expr = zalloc(sizeof(expr_t));
        expr->value = constant->value;
        
        return expr;
    }
    else {
        char *error;
        double value = strtod(input, &error);
        if (error[0]) {
            DEBUG_EXPR("invalid token %s", input);
            return NULL;
        }

        expr_t *expr = zalloc(sizeof(expr_t));
        expr->value = value;
        
        return expr;
    }
}

int check_loops_rec(port_t *the_port, int level, expr_t *expr) {
    int i, l;
    port_t *port;

    if (expr->port_id) {
        if ((port = port_find_by_id(expr->port_id))) {
            /* a loop is detected when we stumble upon the initial port
             * at a level deeper than 1 */
            if (port == the_port && level > 1) {
                return level;
            }

            /* avoid visiting the same port twice */
            if (port->aux) {
                return 0;
            }
            
            port->aux = 1;
        
            if (port->expr && (l = check_loops_rec(the_port, level + 1, port->expr))) {
                return l;
            }
        }

        return 0;
    }

    for (i = 0; i < expr->argc; i++) {
        if ((l = check_loops_rec(the_port, level, expr->args[i]))) {
            return l;
        }
    }
    
    return 0;
}


expr_t *expr_parse(char *port_id, char *input, int len) {
    if (!len) {
        DEBUG_EXPR("empty expression");
        return NULL;
    }

    int level = 0, pos = 0, c;
    char *b = NULL, *e = NULL, *s = input;
    int i, argc = 0;
    char *argp[MAX_ARGS + 1];

    /* skip leading whitespace */
    while (*s && isspace((int) *s) && pos < len) {
        s++;
        pos++;
    }

    char name[MAX_NAME_LEN] = {0};
    while ((c = *s) && isidornum(c) && pos < len) {
        append_max_len(name, c, MAX_NAME_LEN);
        s++;
        pos++;
    }

    /* find beginning and end of function arguments */
    while ((c = *s) && pos < len) {
        if (c == '(') {
            level++;
            
            if (level == 1) {
                if (b) {
                    DEBUG_EXPR("unexpected '%c' at position %d", c, (int) (s - input) + 1);
                    return NULL;
                }

                argp[0] = s;
                b = s + 1;
                argc = 1;
            }
        }
        else if (c == ')') {
            if (level <= 0) {
                DEBUG_EXPR("unexpected '%c' at position %d", c, (int) (s - input) + 1);
                return NULL;
            }
            else if (level == 1) {
                argp[argc] = s;
                e = s - 1;
            }

            level--;
        }
        else if (c == ',' && level == 1) {
            if (argc < MAX_ARGS) {
                argp[argc++] = s;
            }
        }

        s++;
        pos++;
    }

    if (b) { /* function call */
        if (!e) {
            DEBUG_EXPR("unexpected end of expression");
            return NULL;
        }

        if (!isalnum((int) name[0])) {
            DEBUG_EXPR("invalid function name %s", name);
            return NULL;
        }
        
        if (e < b) { /* special no arguments case */
            argc = 0;
        }

        expr_t *args[argc];

        for (i = 0; i < argc; i++) {
            if (!(args[i] = expr_parse(port_id, argp[i] + 1, argp[i + 1] - argp[i] - 1))) {
                /* an error occurred, free everything and give up */
                while (i > 0) expr_free(args[--i]);
                return NULL;
            }
        }

        func_t *func = find_func_by_name(name);
        if (!func) {
            DEBUG_EXPR("no such function %s", name);
            while (argc > 0) expr_free(args[--argc]);
            return NULL;
        }
        
        if ((func->argc >= 0 && func->argc != argc) || (func->argc < 0 && -func->argc > argc)) {
            DEBUG_EXPR("invalid number of arguments to function %s", name);
            while (argc > 0) expr_free(args[--argc]);
            return NULL;
        }
        
        expr_t *expr = zalloc(sizeof(expr_t));
        expr->value = UNDEFINED;
        expr->prev_value = UNDEFINED;
        expr->func = func;
        expr->argc = argc;
        if (argc) {
            expr->args = malloc(sizeof(expr_t *) * argc);
            for (i = 0; i < argc; i++) {
                expr->args[i] = args[i];
            }
        }

        return expr;
    }
    else {
        if (name[0] == '$') { /* port id */
            return parse_port_id_expr(port_id, name + 1);
        }
        else { /* constant */
            return parse_const_expr(name);
        }
    }
}

double expr_eval(expr_t *expr) {
    if (expr->func) { /* function */
        int i;
        double eval_args[expr->argc];
        for (i = 0; i < expr->argc; i++) {
            if (IS_UNDEFINED(eval_args[i] = expr_eval(expr->args[i]))) {
                /* if any of the inner expressions is undefined, the outer expression itself is undefined */
                return UNDEFINED;
            }
        }

        return ((func_t *) expr->func)->callback(expr, expr->argc, eval_args);
    }
    else if (expr->port_id) { /* port value */
        port_t *port = port_find_by_id(expr->port_id);
        if (port && IS_ENABLED(port)) {
            return port->value;
        }
        else {
            return UNDEFINED;
        }
    }
    else { /* assuming constant */
        return expr->value;
    }
}

void expr_free(expr_t *expr) {
    int i;
    for (i = 0; i < expr->argc; i++) {
        expr_free(expr->args[i]);
    }
    if (expr->args) {
        free(expr->args);
    }
    if (expr->port_id) {
        free(expr->port_id);
    }
    free(expr);
}

int expr_check_loops(expr_t *expr, struct port *the_port) {
    port_t *p, **port = all_ports;
    
    /* initialize aux (seen) flag */
    while ((p = *port++)) {
        p->aux = 0;
    }
    the_port->aux = 1;
    
    return check_loops_rec(the_port, 1, expr);
}

struct port **expr_port_deps(expr_t *expr) { // TODO this should return a bitmask of port_ids
    port_t **ports = NULL, **subports, **port, *p;
    int i, size = 0;
    
    if (expr->func) {
        for (i = 0; i < expr->argc; i++) {
            subports = expr_port_deps(expr->args[i]);
            if (subports) {
                port = subports;
                while ((p = *port++)) {
                    ports = realloc(ports, sizeof(port_t *) * (size + 1));
                    ports[size++] = p;
                }
                
                free(subports);
            }
        }
    }
    else if (expr->port_id && (p = port_find_by_id(expr->port_id))) {
        ports = malloc(sizeof(port_t *) * (size + 1));
        ports[size++] = p;
    }
    
    if (ports) {
        ports = realloc(ports, sizeof(port_t *) * (size + 1));
        ports[size] = NULL;
    }

    return ports;
}

bool expr_is_time_dep(expr_t *expr) {
    if (expr->func) {
        if (expr->func == &_time) {
            return TRUE;
        }

        int i;
        for (i = 0; i < expr->argc; i++) {
            if (expr_is_time_dep(expr->args[i])) {
                return TRUE;
            }
        }
    }
    
    return FALSE;
}

bool expr_is_time_ms_dep(expr_t *expr) {
    if (expr->func) {
        if (expr->func == &_timems || expr->func == &_held || expr->func == &_delay) {
            return TRUE;
        }

        int i;
        for (i = 0; i < expr->argc; i++) {
            if (expr_is_time_ms_dep(expr->args[i])) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

bool expr_is_rounding(expr_t *expr) {
    return expr->func == &_floor || expr->func == &_ceil || (expr->func == &_round && expr->argc == 1);
}
