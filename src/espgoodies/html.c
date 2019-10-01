
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

#include <mem.h>
#include <c_types.h>
#include <spi_flash.h>
#include <user_interface.h>

#include "espgoodies/common.h"
#include "espgoodies/html.h"


#define USER1_FLASH_HTML_ADDR       0x78000
#define USER2_FLASH_HTML_ADDR       0xF8000


uint8 *html_load(uint32 *len) {
    uint32 flash_html_addr = system_upgrade_userbin_check() ? USER2_FLASH_HTML_ADDR : USER1_FLASH_HTML_ADDR;

    if (spi_flash_read(flash_html_addr, len, 4) != SPI_FLASH_RESULT_OK) {
        DEBUG_HTML("failed to read HTML length from flash at 0x%05X", flash_html_addr);
        return NULL;
    }

    DEBUG_HTML("HTML length is %d bytes", *len);
    if (*len > 16384) {
        DEBUG_HTML("refusing to read HTML longer than 16k");
        return NULL;
    }

    uint8 *data = malloc(*len);
    uint32 addr = flash_html_addr + 4;
    if (spi_flash_read(addr, (uint32 *) data, *len) != SPI_FLASH_RESULT_OK) {
        DEBUG_HTML("failed to read %d bytes from flash at 0x%05X", *len, addr);
        free(data);
        return NULL;
    }

    DEBUG_HTML("successfully read %d bytes from flash at 0x%05X", *len, flash_html_addr);

    return data;
}
