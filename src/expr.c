
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
#define MAX_HIST_LEN    32


typedef struct {

    double      value;
    uint64      time_ms;

} value_hist_t;

typedef struct {

    char          * name;
    double          value;

} literal_t;


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
ICACHE_FLASH_ATTR static double     _avg_callback(expr_t *expr, int argc, double *args);

ICACHE_FLASH_ATTR static double     _floor_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _ceil_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _round_callback(expr_t *expr, int argc, double *args);

ICACHE_FLASH_ATTR static double     _time_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _timems_callback(expr_t *expr, int argc, double *args);

ICACHE_FLASH_ATTR static double     _delay_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _held_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _deriv_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _integ_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static bool       _filter_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _fmavg_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _fmedian_callback(expr_t *expr, int argc, double *args);

ICACHE_FLASH_ATTR static double     _acc_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _hyst_callback(expr_t *expr, int argc, double *args);
ICACHE_FLASH_ATTR static double     _sequence_callback(expr_t *expr, int argc, double *args);

ICACHE_FLASH_ATTR expr_t          * parse_rec(char *port_id, char *input, int len, int abs_pos);
ICACHE_FLASH_ATTR static expr_t   * parse_port_id_expr(char *port_id, char *input, int abs_pos);
ICACHE_FLASH_ATTR static expr_t   * parse_literal_expr(char *input, int abs_pos);
ICACHE_FLASH_ATTR static void       set_parse_error(char *reason, char *token, int32 pos);
ICACHE_FLASH_ATTR static literal_t* find_literal_by_name(char *name);
ICACHE_FLASH_ATTR static func_t   * find_func_by_name(char *name);
ICACHE_FLASH_ATTR static int        check_loops_rec(port_t *the_port, int level, expr_t *expr);
ICACHE_FLASH_ATTR static bool       func_needs_free(expr_t *expr);


static literal_t _false = {.name = "false", .value = 0};
static literal_t _true =  {.name = "true",  .value = 1};

static literal_t *literals[] = {
    &_false,
    &_true,
    NULL
};

static expr_parse_error_t           parse_error;


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

double _avg_callback(expr_t *expr, int argc, double *args) {
    int i;
    double s = 0;
    for (i = 0; i < argc; i++) {
        s += args[i];
    }

    return s / argc;
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
    return system_uptime_ms();
}

double _delay_callback(expr_t *expr, int argc, double *args) {
    if (argc < 1) { /* Called from expr_free() */
        if (expr->paux) {
            free(expr->paux);
            expr->paux = NULL;
        }
        return FALSE;
    }

    uint64 time_ms = system_uptime_ms();
    double value = args[0];
    double delay = args[1];
    double result = UNDEFINED;
    int i;

    value_hist_t *hist = expr->paux;

    /* Detect value transitions and push them to history */
    if (value != expr->value) {
        expr->value = value;

        if (expr->len == MAX_HIST_LEN) { /* Shift elements in queue to make place for new value */
            for (i = 0; i < expr->len - 1; i++) {
                hist[i] = hist[i + 1];
            }
        }
        else { /* Grow history for another value */
            expr->len++;
            expr->paux = hist = realloc(expr->paux, sizeof(value_hist_t) * expr->len);
        }

        hist[expr->len - 1].value = value;
        hist[expr->len - 1].time_ms = time_ms;
    }

    /* Go through history (forward) and find the first value that is newer than the delay; use it as a result; keep only
     * newer values in history */
    bool resized = FALSE;
    while (expr->len && (time_ms - hist[0].time_ms > delay)) {
        result = hist[0].value;
        for (i = 0; i < expr->len - 1; i++) {
            hist[i] = hist[i + 1];
        }
        expr->len--;
        resized = TRUE;
    }

    if (resized) {
        if (expr->len) {
            expr->paux = realloc(expr->paux, sizeof(value_hist_t) * expr->len);
        }
        else {
            free(expr->paux);
            expr->paux = NULL;
        }
    }

    return result;
}

double _held_callback(expr_t *expr, int argc, double *args) {
    uint64 time_ms = system_uptime_ms();
    double value = args[0];
    double fixed_value = args[1];
    int duration = args[2];
    bool result = FALSE;

    if (!expr->aux) { /* Very first expression eval call */
        /* aux flag is used as time counter, in milliseconds */
        expr->aux = time_ms;
    }
    else {
        uint64 delta = time_ms - expr->aux;

        if ((expr->value != value) /* Value changed */) {
            /* Reset held timer */
            expr->aux = time_ms;
        }
        else {
            result = (delta >= duration) && (value == fixed_value);
        }
    }

    expr->value = value;

    return result;
}

double _deriv_callback(expr_t *expr, int argc, double *args) {
    uint64 time_ms = system_uptime_us() / 1000;
    double value = args[0];
    double sampling_interval = args[1];
    double result = 0;

    if (expr->aux) { /* Not the very first expression eval call */
        uint32 delta_time_ms = time_ms - expr->aux;
        if (delta_time_ms < sampling_interval) {
            return UNDEFINED;
        }

        result = 1000.0 * (value - expr->value) / delta_time_ms;
    }

    expr->value = value;
    expr->aux = time_ms;

    return result;
}

double _integ_callback(expr_t *expr, int argc, double *args) {
    uint64 time_ms = system_uptime_ms();
    double value = args[0];
    double accumulator = args[1];
    double sampling_interval = args[2];
    double result = accumulator;

    if (expr->aux) { /* Not the very first expression eval call */
        uint32 delta_time_ms = time_ms - expr->aux;
        if (delta_time_ms < sampling_interval) {
            return UNDEFINED;
        }

        result += (value + expr->value) * delta_time_ms / 2000.0; /* 2 -> mean, 1000 -> millis */
    }

    expr->value = value;
    expr->aux = time_ms;

    return result;
}

bool _filter_callback(expr_t *expr, int argc, double *args) {
    if (argc < 1) { /* Called from expr_free() */
        if (expr->paux) {
            free(expr->paux);
            expr->paux = NULL;
        }
        return FALSE;
    }

    int i;
    uint64 time_ms = system_uptime_ms();
    double value = args[0];
    double *values = expr->paux; /* expr->paux flag is used as a pointer to a value history queue */
    int width = (int) args[1];
    int sampling_interval = args[2];

    /* Limit width to reasonable values */
    if (width > MAX_HIST_LEN) {
        width = MAX_HIST_LEN;
    }
    if (width < 1) {
        width = 1;
    }

    /* Very first expression eval call */
    if (!expr->paux) {
        expr->paux = values = malloc(sizeof(double));
        expr->len = 1;
        expr->aux = time_ms;
        values[0] = value;

        return TRUE;
    }

    /* Don't evaluate if below sampling interval */
    int32 delta_ms = time_ms - expr->aux;
    if (delta_ms < sampling_interval) {
        return FALSE;
    }

    expr->aux = time_ms;

    int extra = expr->len - width + 1;
    bool resized = FALSE;
    if (extra > 0) { /* Drop extra elements from queue */
        for (i = 0; i < expr->len - extra; i++) {
            values[i] = values[i + extra];
        }

        /* Shrink history if width decreased */
        if (expr->len > width) {
            expr->len = width;
            resized = TRUE;
        }
    }
    else { /* Grow history for another element */
        expr->len++;
        resized = TRUE;
    }

    if (resized) {
        expr->paux = values = realloc(expr->paux, sizeof(double) * expr->len);
    }
    values[expr->len - 1] = value;

    return TRUE;
}

double _fmavg_callback(expr_t *expr, int argc, double *args) {
    if (!_filter_callback(expr, argc, args)) {
        return UNDEFINED;
    }

    /* Apply moving average filter */
    int i;
    double s = 0;
    double *values = expr->paux;
    for (i = 0; i < expr->len; i++) {
        s += values[i];
    }

    return s / expr->len;
}

double _fmedian_callback(expr_t *expr, int argc, double *args) {
    if (!_filter_callback(expr, argc, args)) {
        return UNDEFINED;
    }

    /* When dealing with less than 3 values, simply return the first one */
    if (expr->len < 3) {
        return ((double *) expr->paux)[0];
    }

    /* Copy the values to a temporary buffer, so that they can be sorted out-of-place */
    double *values = malloc(sizeof(double) * expr->len);
    memcpy(values, expr->paux, sizeof(double) * expr->len);

    /* Apply median filter */
    qsort(values, expr->len, sizeof(double), compare_double);
    double result = values[expr->len / 2];
    free(values);

    return result;
}

double _acc_callback(expr_t *expr, int argc, double *args) {
    double value = args[0];
    double accumulator = args[1];
    double result = accumulator;

    if (!IS_UNDEFINED(expr->value)) { /* Not the very first expression eval call */
        result += value - expr->value;
    }

    expr->value = value;

    return result;
}

double _hyst_callback(expr_t *expr, int argc, double *args) {
    double value = args[0];
    double threshold1 = args[1];
    double threshold2 = args[2];

    /* expr->value is used as last value */

    if (IS_UNDEFINED(expr->value)) { /* Very first expression eval call */
        expr->value = 0;
    }

    expr->value = ((expr->value == 0 && value > threshold2) ||
                   (expr->value != 0 && value >= threshold1));

    return expr->value;
}

double _sequence_callback(expr_t *expr, int argc, double *args) {
    int64 time_ms = system_uptime_ms();
    double result = args[0];
    int delta = time_ms - expr->aux;

    if (!expr->aux || delta < 0) { /* Very first expression eval call or system time overflow */
        /* aux flag is used to store the initial reference time, in milliseconds */
        expr->aux = time_ms;
    }
    else {
        int num_values = argc / 2; /* We have pairs of values and delays */
        int i, delay_so_far = 0, total_delay = 0;

        /* Compute the total delay */
        for (i = 0; i < num_values; i++) {
            if (2 * i + 1 < argc) {
                total_delay += args[2 * i + 1];
            }
        }

        /* Always work modulo total_delay, to create repeat effect */
        delta = delta % (int) total_delay;

        for (i = 0; i < num_values; i++) {
            delay_so_far += (2 * i + 1) < argc ? args[2 * i + 1] : 0;
            if (delay_so_far >= delta) {
                result = args[2 * i];
                break;
            }
        }
    }

    return result;
}


func_t _add =      {.name = "ADD",      .argc = -2, .callback = _add_callback};
func_t _sub =      {.name = "SUB",      .argc = 2,  .callback = _sub_callback};
func_t _mul =      {.name = "MUL",      .argc = -2, .callback = _mul_callback};
func_t _div =      {.name = "DIV",      .argc = 2,  .callback = _div_callback};
func_t _mod =      {.name = "MOD",      .argc = 2,  .callback = _mod_callback};

func_t _and =      {.name = "AND",      .argc = -2, .callback = _and_callback};
func_t _or =       {.name = "OR",       .argc = -2, .callback = _or_callback};
func_t _not =      {.name = "NOT",      .argc = 1,  .callback = _not_callback};
func_t _xor =      {.name = "XOR",      .argc = 2,  .callback = _xor_callback};

func_t _bitand =   {.name = "BITAND",   .argc = 2,  .callback = _bitand_callback};
func_t _bitor =    {.name = "BITOR",    .argc = 2,  .callback = _bitor_callback};
func_t _bitnot =   {.name = "BITNOT",   .argc = 1,  .callback = _bitnot_callback};
func_t _bitxor =   {.name = "BITXOR",   .argc = 2,  .callback = _bitxor_callback};
func_t _shl =      {.name = "SHL",      .argc = 2,  .callback = _shl_callback};
func_t _shr =      {.name = "SHR",      .argc = 2,  .callback = _shr_callback};

func_t _if =       {.name = "IF",       .argc = 3,  .callback = _if_callback};
func_t _eq =       {.name = "EQ",       .argc = 2,  .callback = _eq_callback};
func_t _gt =       {.name = "GT",       .argc = 2,  .callback = _gt_callback};
func_t _gte =      {.name = "GTE",      .argc = 2,  .callback = _gte_callback};
func_t _lt =       {.name = "LT",       .argc = 2,  .callback = _lt_callback};
func_t _lte =      {.name = "LTE",      .argc = 2,  .callback = _lte_callback};

func_t _abs =      {.name = "ABS",      .argc = 1,  .callback = _abs_callback};
func_t _sgn =      {.name = "SGN",      .argc = 1,  .callback = _sgn_callback};

func_t _min =      {.name = "MIN",      .argc = -2, .callback = _min_callback};
func_t _max =      {.name = "MAX",      .argc = -2, .callback = _max_callback};
func_t _avg =      {.name = "AVG",      .argc = -2, .callback = _avg_callback};

func_t _floor =    {.name = "FLOOR",    .argc = 1,  .callback = _floor_callback};
func_t _ceil =     {.name = "CEIL",     .argc = 1,  .callback = _ceil_callback};
func_t _round =    {.name = "ROUND",    .argc = -1, .callback = _round_callback};

func_t _time =     {.name = "TIME",     .argc = 0,  .callback = _time_callback};
func_t _timems =   {.name = "TIMEMS",   .argc = 0,  .callback = _timems_callback};

func_t _delay =    {.name = "DELAY",    .argc = 2,  .callback = _delay_callback};
func_t _held =     {.name = "HELD",     .argc = 3,  .callback = _held_callback};
func_t _deriv =    {.name = "DERIV",    .argc = 2,  .callback = _deriv_callback};
func_t _integ =    {.name = "INTEG",    .argc = 3,  .callback = _integ_callback};
func_t _fmavg =    {.name = "FMAVG",    .argc = 3,  .callback = _fmavg_callback};
func_t _fmedian =  {.name = "FMEDIAN",  .argc = 3,  .callback = _fmedian_callback};

func_t _acc =      {.name = "ACC",      .argc = 2,  .callback = _acc_callback};
func_t _hyst =     {.name = "HYST",     .argc = 3,  .callback = _hyst_callback};
func_t _sequence = {.name = "SEQUENCE", .argc = -2, .callback = _sequence_callback};

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
    &_avg,

    &_floor,
    &_ceil,
    &_round,

    &_time,
    &_timems,

    &_delay,
    &_held,
    &_deriv,
    &_integ,
    &_fmavg,
    &_fmedian,

    &_acc,
    &_hyst,
    &_sequence,

    NULL
};


expr_t *parse_rec(char *port_id, char *input, int len, int abs_pos) {
    if (!len) {
        DEBUG_EXPR("empty expression");
        set_parse_error("empty-expression", /* token = */ NULL, /* pos = */ -1);
        return NULL;
    }

    int level = 0, pos = 0, skip_pos = 0, c, l;
    char *b = NULL, *e = NULL, *s = input;
    int i, argc = 0;
    char *argp[MAX_ARGS];
    uint32 arg_pos[MAX_ARGS];

    /* Skip leading whitespace */
    while (*s && isspace((int) *s) && pos < len) {
        s++;
        pos++;
        skip_pos++;
    }

    char name[MAX_NAME_LEN] = {0};
    while ((c = *s) && (c != '(') && (pos < len)) {
        append_max_len(name, c, MAX_NAME_LEN);
        s++;
        pos++;
    }

    /* Find beginning and end of function arguments */
    while ((c = *s) && pos < len) {
        if (c == '(') {
            level++;

            if (level == 1) {
                if (b) {
                    DEBUG_EXPR("unexpected \"%c\" at position %d", c, abs_pos + pos);
                    set_parse_error("unexpected-character", /* token = */ NULL, abs_pos + pos);
                    return NULL;
                }

                argp[0] = s;
                arg_pos[0] = pos;
                b = s + 1;
                argc = 1;
            }
        }
        else if (c == ')') {
            if (level <= 0) {
                DEBUG_EXPR("unexpected \"%c\" at position %d", c, abs_pos + pos);
                set_parse_error("unbalanced-parentheses", /* token = */ NULL, abs_pos + pos);
                return NULL;
            }
            else if (level == 1) {
                argp[argc] = s;
                arg_pos[argc] = pos;
                e = s - 1;
            }

            level--;
        }
        else if (c == ',' && level == 1) {
            if (argc < MAX_ARGS) {
                arg_pos[argc] = pos;
                argp[argc++] = s;
            }
        }

        s++;
        pos++;
    }

    if (b) { /* We have parentheses => function call */
        if (!e) {
            DEBUG_EXPR("unexpected end of expression");
            set_parse_error("unexpected-end", /* token = */ NULL, /* pos = */ -1);
            return NULL;
        }

        if (!isalnum((int) name[0])) {
            DEBUG_EXPR("invalid function name \"%s\"", name);
            set_parse_error("unexpected-character", /* token = */ NULL, abs_pos + skip_pos);
            return NULL;
        }

        if (e < b) { /* Special no arguments case */
            argc = 0;
        }

        expr_t *args[argc];

        for (i = 0; i < argc; i++) {
            l = argp[i + 1] - argp[i] - 1;
            if (l == 0) {
                DEBUG_EXPR("empty argument");
                /* When encountering empty argument expression, following character is unexpected */
                set_parse_error("unexpected-character", /* token = */ NULL, abs_pos + arg_pos[i] + 1);
                while (i > 0) expr_free(args[--i]);
                return NULL;
            }
            if (!(args[i] = parse_rec(port_id, argp[i] + 1, l, abs_pos + arg_pos[i] + 1))) {
                /* An error occurred, free everything and give up */
                while (i > 0) expr_free(args[--i]);
                return NULL;
            }
        }

        func_t *func = find_func_by_name(name);
        if (!func) {
            DEBUG_EXPR("no such function \"%s\"", name);
            set_parse_error("unknown-function", /* token = */ name, abs_pos + skip_pos);
            while (argc > 0) expr_free(args[--argc]);
            return NULL;
        }

        if ((func->argc >= 0 && func->argc != argc) || (func->argc < 0 && -func->argc > argc)) {
            DEBUG_EXPR("invalid number of arguments to function \"%s\"", name);
            set_parse_error("invalid-number-of-arguments", /* token = */ name, abs_pos + skip_pos);
            while (argc > 0) expr_free(args[--argc]);
            return NULL;
        }

        expr_t *expr = zalloc(sizeof(expr_t));
        expr->value = UNDEFINED;
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
        if (name[0] == '$') { /* Port id */
            return parse_port_id_expr(port_id, name + 1, abs_pos + skip_pos);
        }
        else { /* Literal value */
            return parse_literal_expr(name, abs_pos + skip_pos);
        }
    }
}

expr_t *parse_port_id_expr(char *port_id, char *input, int abs_pos) {
    int c;
    char *s = input;
    
    if (input[0] && !isalpha((int) input[0]) && input[0] != '_') {
        DEBUG_EXPR("invalid port identifier \"%s\"", input);
        set_parse_error("unexpected-character", /* token = */ NULL, abs_pos);
        return NULL;
    }

    while ((c = *s++)) {
        if (!isalnum((int) c) && c != '_' && c != '-') {
            DEBUG_EXPR("invalid port identifier \"%s\"", input);
            set_parse_error("unexpected-character", /* token = */ NULL, abs_pos + s - input);
            return NULL;
        }
    }
    
    expr_t *expr = zalloc(sizeof(expr_t));
    expr->value = UNDEFINED;
    
    if (*input) {
        expr->port_id = strdup(input);
    }
    else { /* Reference to port itself */
        expr->port_id = strdup(port_id);
    }

    return expr;
}

expr_t *parse_literal_expr(char *input, int abs_pos) {
    literal_t *literal = find_literal_by_name(input);
    if (literal) {
        expr_t *expr = zalloc(sizeof(expr_t));
        expr->value = literal->value;
        
        return expr;
    }
    else {
        char *error;
        double value = strtod(input, &error);
        if (error[0]) {
            DEBUG_EXPR("invalid token \"%s\"", input);
            set_parse_error("unexpected-character", /* token = */ NULL, abs_pos + error - input);
            return NULL;
        }

        expr_t *expr = zalloc(sizeof(expr_t));
        expr->value = value;
        
        return expr;
    }
}

void set_parse_error(char *reason, char *token, int32 pos) {
    if (parse_error.token) {
        free(parse_error.token);
        parse_error.token = NULL;
    }

    if (token) {
        parse_error.token = strdup(token);
    }

    parse_error.reason = reason;
    parse_error.pos = pos;
}

literal_t *find_literal_by_name(char *name) {
    literal_t **literal = literals, *c;
    while ((c = *literal++)) {
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


int check_loops_rec(port_t *the_port, int level, expr_t *expr) {
    int i, l;
    port_t *port;

    if (expr->port_id) {
        if ((port = port_find_by_id(expr->port_id))) {
            /* A loop is detected when we stumble upon the initial port at a level deeper than 1 */
            if (port == the_port && level > 1) {
                return level;
            }

            /* Avoid visiting the same port twice */
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

bool func_needs_free(expr_t *expr) {
    return (expr->func == &_fmavg ||
            expr->func == &_fmedian ||
            expr->func == &_delay);
}


expr_t *expr_parse(char *port_id, char *input, int len) {
    return parse_rec(port_id, input, len, /* abs_pos = */ 1);
}

expr_parse_error_t *expr_parse_get_error(void) {
    return &parse_error;
}

double expr_eval(expr_t *expr) {
    if (expr->func) { /* Function */
        int i;
        double eval_args[expr->argc];
        for (i = 0; i < expr->argc; i++) {
            if (IS_UNDEFINED(eval_args[i] = expr_eval(expr->args[i]))) {
                /* If any of the inner expressions is undefined, the outer expression itself is undefined */
                return UNDEFINED;
            }
        }

        return ((func_t *) expr->func)->callback(expr, expr->argc, eval_args);
    }
    else if (expr->port_id) { /* Port value */
        port_t *port = port_find_by_id(expr->port_id);
        if (port && IS_ENABLED(port)) {
            return port->value;
        }
        else {
            return UNDEFINED;
        }
    }
    else { /* Assuming literal value */
        return expr->value;
    }
}

void expr_free(expr_t *expr) {
    int i;
    for (i = 0; i < expr->argc; i++) {
        expr_free(expr->args[i]);
    }
    if (func_needs_free(expr)) {
        ((func_t *) expr->func)->callback(expr, -1, NULL);
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
    
    /* Initialize aux (seen) flag */
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
        if (expr->func == &_timems ||
            expr->func == &_held ||
            expr->func == &_delay ||
            expr->func == &_deriv ||
            expr->func == &_integ||
            expr->func == &_sequence ||
            expr->func == &_fmavg ||
            expr->func == &_fmedian) {

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
