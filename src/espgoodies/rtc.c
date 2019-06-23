
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

#include <user_interface.h>

#include "common.h"
#include "rtc.h"


#define FULL_BOOT_MAGIC         0xdeadbeef
#define FULL_BOOT_TEST_ADDR     128     /* 128 * 4 bytes = 512 */
#define BOOT_COUNT_ADDR         129     /* 129 * 4 bytes = 516 */


static bool                     full_boot = TRUE;
static int                      boot_count = 0;


void rtc_init() {
    uint32 test_value;
    system_rtc_mem_read(FULL_BOOT_TEST_ADDR, &test_value, 4);

    if (test_value != FULL_BOOT_MAGIC) {
        DEBUG_RTC("full boot");
        test_value = FULL_BOOT_MAGIC;
        system_rtc_mem_write(FULL_BOOT_TEST_ADDR, &test_value, 4);
        full_boot = TRUE;
    }
    else {
        system_rtc_mem_read(BOOT_COUNT_ADDR, &boot_count, 4);
        boot_count++;
        DEBUG_RTC("light boot (count = %d)", boot_count);
        full_boot = FALSE;
    }

    system_rtc_mem_write(BOOT_COUNT_ADDR, &boot_count, 4);
}

void rtc_reset() {
    uint32 value = 0;

    DEBUG_RTC("resetting");
    system_rtc_mem_write(FULL_BOOT_TEST_ADDR, &value, 4);
    system_rtc_mem_write(BOOT_COUNT_ADDR, &value, 4);
}

bool rtc_is_full_boot() {
    return full_boot;
}

int rtc_get_boot_count() {
    return boot_count;
}

int rtc_get_value(uint8 addr) {
    if (full_boot) {
        /* in case of full boot, RTC memory contents are random uninitialized crap,
         * so the best we can do here is to return 0 */
        return 0;
    }

    uint32 value;
    system_rtc_mem_read(addr, &value, 4);

    return value;
}

void rtc_set_value(uint8 addr, int value) {
    DEBUG_RTC("setting value at %d * 4 = %d", addr, value);

    system_rtc_mem_write(addr, &value, 4);
}
