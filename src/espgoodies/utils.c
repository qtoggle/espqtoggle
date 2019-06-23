
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
#include <string.h>
#include <mem.h>
#include <ctype.h>
#include <limits.h>
#include <user_interface.h>
#include <espconn.h>

#include "common.h"
#include "utils.h"


#define REALLOC_CHUNK_SIZE      32
#define DTOSTR_BUF_LEN          32

#if defined(_DEBUG) && defined(_DEBUG_IP)
static struct espconn         * debug_udp_conn = NULL;
ICACHE_FLASH_ATTR static void   debug_udp_send(char *buf, int len);
#endif


void append_max_len(char *s, char c, int max_len) {
    // TODO improve this by supplying the current length
    int len = strlen(s);
    if (len < max_len) {
        s[len] = c;
        s[len + 1] = 0;
    }
}

int realloc_chunks(char **p, int current_size, int req_size) {
    while (current_size < req_size) {
        current_size += REALLOC_CHUNK_SIZE;
        *p = realloc(*p, current_size);
    }

    return current_size;
}

double strtod(const char *s, char **endptr) {
    double d = 0;
    int c, decimals, sign = 0, i = 0;

    while (isspace((int) *s)) {
        s++;
    }

    /* detect and skip sign */
    if (*s == '-') {
        sign = 1;
        s++;
    }

    decimals = -1;
    for (i = 0; (c =*s); i++, s++) {
        if (c == '.' && decimals == -1) {
            decimals = i;
        }
        else if (isdigit(c)) {
            d = d * 10 + c - '0';
        }
        else {
            if (endptr) {
                *endptr = (char *) s;
                return 0;
            }
        }
    }

    if (decimals >= 0) {
        decimals = i - decimals - 1;
        while (decimals--) {
            d /= 10;
        }
    }

    if (sign) {
        d = -d;
    }

    if (endptr) {
        *endptr = (char *) s;
    }

    return d;
}

char *dtostr(double d, int8 decimals) {
    int i, len = 0;
    bool auto_decimals = FALSE;
    bool sign = FALSE;
    char c;
    uint64 n;
    static char dtostr_buf[DTOSTR_BUF_LEN];

    /* process the sign */
    if (d < 0) {
        d = -d;
        sign = TRUE;
    }

    if (decimals < 0) {
        decimals = 10; /* analyze up to 10 decimals */
        auto_decimals = TRUE;
    }

    if (decimals >= 0) {
        /* if the number of decimals is too big,
         * we risk having an integer overflow here! */
        for (i = 0; i < decimals; i++) {
            d *= 10;
        }

        n = round(d);
        if (n) {
            for (i = 0; n > 0 && len < DTOSTR_BUF_LEN - 1; i++) {
                if (i == decimals && decimals) {
                    dtostr_buf[len++] = '.';
                }
                dtostr_buf[len++] = (n % 10) + '0';
                n /= 10;
            }
            if (i == decimals) {
                dtostr_buf[len++] = '.';
                dtostr_buf[len++] = '0';
            }
        }
        else {
            dtostr_buf[len++] = '0';
            for (i = 0; i < decimals; i++) {
                if (i == decimals - 1) {
                    dtostr_buf[len++] = '.';
                }
                dtostr_buf[len++] = '0';
            }
        }
    }

    /* add sign */
    if (sign) {
        dtostr_buf[len++] = '-';
    }

    /* reverse the string  */
    for (i = 0; i < len / 2; i++) {
        c = dtostr_buf[i];
        dtostr_buf[i] = dtostr_buf[len - i - 1];
        dtostr_buf[len - i - 1] = c;
    }

    if (auto_decimals) {
        while (dtostr_buf[len - 1] == '0') {
            dtostr_buf[--len] = 0;
        }

        if (dtostr_buf[len - 1] == '.') {
            dtostr_buf[--len] = 0;
        }
    }
    else {
        dtostr_buf[len] = 0;
    }

    return dtostr_buf;
}

double decent_round(double d) {
    d = fabs(d);
    if (d >= 1000) {
        return round(d);
    }
    else if (d >= 100) {
        return round(d * 100) / 100;
    }
    else if (d >= 10) {
        return round(d * 1000) / 1000;
    }
    else {
        return round(d * 10000) / 10000;
    }
}

int gpio_get_mux(int gpio_no) {
    switch (gpio_no) {
        case 0:
            return PERIPHS_IO_MUX_GPIO0_U;

        case 1:
            return PERIPHS_IO_MUX_U0TXD_U;

        case 2:
            return PERIPHS_IO_MUX_GPIO2_U;

        case 3:
            return PERIPHS_IO_MUX_U0RXD_U;

        case 4:
            return PERIPHS_IO_MUX_GPIO4_U;

        case 5:
            return PERIPHS_IO_MUX_GPIO5_U;

        case 9:
            return PERIPHS_IO_MUX_SD_DATA2_U;

        case 10:
            return PERIPHS_IO_MUX_SD_DATA3_U;

        case 12:
            return PERIPHS_IO_MUX_MTDI_U;

        case 13:
            return PERIPHS_IO_MUX_MTCK_U;

        case 14:
            return PERIPHS_IO_MUX_MTMS_U;

        case 15:
            return PERIPHS_IO_MUX_MTDO_U;

        default:
            return 0;
    }
}

int gpio_get_func(int gpio_no) {
    switch (gpio_no) {
        case 0:
            return FUNC_GPIO0;

        case 1:
            return FUNC_GPIO1;

        case 2:
            return FUNC_GPIO2;

        case 3:
            return FUNC_GPIO3;

        case 4:
            return FUNC_GPIO4;

        case 5:
            return FUNC_GPIO5;

        case 9:
            return FUNC_GPIO9;

        case 10:
            return FUNC_GPIO10;

        case 12:
            return FUNC_GPIO12;

        case 13:
            return FUNC_GPIO13;

        case 14:
            return FUNC_GPIO14;

        case 15:
            return FUNC_GPIO15;

        default:
            return 0;
    }
}

void gpio_select_func(int gpio_no) {
    int mux = gpio_get_mux(gpio_no);
    if (!mux) {
        return;
    }

    PIN_FUNC_SELECT(mux, gpio_get_func(gpio_no));
}

void gpio_set_pullup(int gpio_no, bool enabled) {
    int mux = gpio_get_mux(gpio_no);
    if (!mux) {
        return;
    }

    if (enabled) {
        PIN_PULLUP_EN(mux);
    }
    else {
        PIN_PULLUP_DIS(mux);
    }
}


char *my_strtok(char *s, char *d) {
    static char *start = NULL;
    char *p;
    
    if (s) {
        while (*s == *d) {
            *s = 0;
            s++;
        }
        start = s;
    }
    else {
        s = start;
    }
    
    while (*s && *s != *d) {
        s++;
    }

    if (*s == *d) { /* delimiter */
        while (*s == *d) {
            *s = 0;
            s++;
        }
        
        p = start;
        start = s;

        return p;
    }
    else { /* end of string */
        if (s == start) {
            return NULL;
        }

        p = start;
        start = s;
        
        return p;
    }
}

char *my_strdup(const char *s) {
    if (!s) {
        return NULL;
    }

    int len = strlen(s);
    char *d = malloc(len + 1);
    memcpy(d, s, len + 1);
    
    return d;
}

char *my_strndup(const char *s, int n) {
    if (!s) {
        return NULL;
    }

    int len = MIN(strlen(s), n);
    char *d = malloc(len + 1);
    memcpy(d, s, len);
    d[len] = 0;

    return d;
}

#if defined(_DEBUG) && defined(_DEBUG_IP)

int udp_printf(const char *format, ...) {
    va_list args;
    static char buf[256];
    int size;

    va_start(args, format);
    size = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    debug_udp_send(buf, size);

    return size;
}

void debug_udp_send(char *buf, int len) {
    int result;

    if (!debug_udp_conn) {
        /* initialize the debug UDP connection */
        debug_udp_conn = zalloc(sizeof(struct espconn));

        debug_udp_conn->type = ESPCONN_UDP;
        debug_udp_conn->state = ESPCONN_NONE;
        debug_udp_conn->proto.udp = zalloc(sizeof(esp_udp));
        debug_udp_conn->proto.udp->remote_port = _DEBUG_PORT;

        debug_udp_conn->proto.udp->local_port = espconn_port();

        char *p, *s = _DEBUG_IP;
        debug_udp_conn->proto.udp->remote_ip[0] = strtol(s, &p, 10);
        s = p + 1;
        debug_udp_conn->proto.udp->remote_ip[1] = strtol(s, &p, 10);
        s = p + 1;
        debug_udp_conn->proto.udp->remote_ip[2] = strtol(s, &p, 10);
        s = p + 1;
        debug_udp_conn->proto.udp->remote_ip[3] = strtol(s, &p, 10);

        result = espconn_create(debug_udp_conn);
        if (result) {
            os_printf_plus("espconn_create() failed with result %d\n", result);
        }
    }

    result = espconn_sendto(debug_udp_conn, (uint8 *) buf, len);
    if (result) {
        os_printf_plus("espconn_sendto() failed with result %d\n", result);
    }
}

#endif
