
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

#include "common.h"
#include "flashcfg.h"


bool flashcfg_load(uint8 *data) {
    if (spi_flash_read(FLASH_CONFIG_ADDR, (uint32 *) data, FLASH_CONFIG_SIZE) != SPI_FLASH_RESULT_OK) {
        DEBUG_FLASHCFG("failed to read %d bytes from flash config at 0x%05X",
                       FLASH_CONFIG_SIZE, FLASH_CONFIG_ADDR);
        return FALSE;
    }
    else {
        DEBUG_FLASHCFG("successfully read %d bytes from flash config at 0x%05X",
                       FLASH_CONFIG_SIZE, FLASH_CONFIG_ADDR);
    }

    return TRUE;
}

bool flashcfg_save(uint8 *data) {
    int i, sector;
    for (i = 0; i < FLASH_CONFIG_SIZE / FLASH_CONFIG_SECTOR_SIZE; i++) {
        sector = (FLASH_CONFIG_ADDR + FLASH_CONFIG_SECTOR_SIZE * i) / 0x1000;
        if (spi_flash_erase_sector(sector) != SPI_FLASH_RESULT_OK) {
            DEBUG_FLASHCFG("failed to erase flash sector at 0x%05X",
                           FLASH_CONFIG_ADDR + FLASH_CONFIG_SECTOR_SIZE * i);
            return FALSE;
        }
        else {
            DEBUG_FLASHCFG("flash sector at 0x%05X erased",
                           FLASH_CONFIG_ADDR + FLASH_CONFIG_SECTOR_SIZE * i);
        }
    }

    if (spi_flash_write(FLASH_CONFIG_ADDR, (uint32 *) data, FLASH_CONFIG_SIZE) != SPI_FLASH_RESULT_OK) {
        DEBUG_FLASHCFG("failed to write %d bytes of flash config at 0x%05X",
                       FLASH_CONFIG_SIZE, FLASH_CONFIG_ADDR);
        return FALSE;
    }
    else {
        DEBUG_FLASHCFG("successfully written %d bytes of flash config at 0x%05X",
                       FLASH_CONFIG_SIZE, FLASH_CONFIG_ADDR);
    }

    return TRUE;
}

bool flashcfg_reset() {
    DEBUG_FLASHCFG("resetting to factory defaults");

    uint8 *config_data = zalloc(FLASH_CONFIG_SIZE);
    bool result = flashcfg_save(config_data);
    free(config_data);

    return result;
}
