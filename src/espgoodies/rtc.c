
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

#include <user_interface.h>

#include "common.h"
#include "rtc.h"


#define FULL_BOOT_MAGIC         0xdeadbeef
#define FULL_BOOT_TEST_ADDR     128     /* 128 * 4 bytes = 512 */
#define BOOT_COUNT_ADDR         129     /* 129 * 4 bytes = 516 */


static bool                     full_boot = TRUE;
static int                      boot_count = 0;


void rtc_init(void) {
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

void rtc_reset(void) {
    uint32 value = 0;

    DEBUG_RTC("resetting");
    system_rtc_mem_write(FULL_BOOT_TEST_ADDR, &value, 4);
    system_rtc_mem_write(BOOT_COUNT_ADDR, &value, 4);
}

bool rtc_is_full_boot(void) {
    return full_boot;
}

int rtc_get_boot_count(void) {
    return boot_count;
}

int rtc_get_value(uint8 addr) {
    if (full_boot) {
        /* in case of full boot, RTC memory contents are random uninitialized crap,
         * so the best we can do here is to return 0 */
        return 0;
    }

    uint32 value;
    if (!system_rtc_mem_read(addr, &value, 4)) {
        DEBUG_RTC("failed to read value at %d * 4", addr);
        return 0;
    }

    return value;
}

bool rtc_set_value(uint8 addr, int value) {
    DEBUG_RTC("setting value at %d * 4 to %d", addr, value);

    if (!system_rtc_mem_write(addr, &value, 4)) {
        DEBUG_RTC("failed to set value at %d * 4 to %d", addr, value);
        return FALSE;
    }

    return TRUE;
}
