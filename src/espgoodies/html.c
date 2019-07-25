
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
