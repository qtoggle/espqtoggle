
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

#include <c_types.h>
#include <spi_flash.h>

#include "common.h"
#include "initdata.h"


const uint32 esp_init_data_default[] = {
    0x02040005,0x02050505,0x05040005,0x05050405,0xFFFDFE04,0xE0F0F0F0,
    0x0AE1E0E0,0x00F8FFFF,0x4E52F8F8,0x3840444A,0x01010000,0x03030302,
    0x00000001,0x00020000,0x00000000,0x00000000,0x000A0AE1,0x00000000,
    0x93010000,0x00000043,0x00000000,0x00000000,0x00000000,0x00000000,
    0x00000000,0x00000000,0x00000000,0x00000000,0x00010000,0x00000000,
    0x00000000,0x00000000
};


void init_data_ensure(void) {
    uint32 rf_cal_sec = 256 - 5;
    uint32 addr = (rf_cal_sec * SPI_FLASH_SEC_SIZE) + 0x1000;
    uint32 rf_cal_data;

    spi_flash_read(addr, &rf_cal_data, 4);
    if (rf_cal_data != esp_init_data_default[0]) {
        DEBUG("flashing ESP init data");
        spi_flash_erase_sector(rf_cal_sec);
        spi_flash_write(addr, (uint32 *) esp_init_data_default, sizeof(esp_init_data_default));
    }
    else {
        DEBUG("ESP init data is up-to-date");
    }
}
