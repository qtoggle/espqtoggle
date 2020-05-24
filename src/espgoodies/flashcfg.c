
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

#include "common.h"
#include "flashcfg.h"


bool flashcfg_load(uint8 *data, uint32 offs, uint32 size) {
    if (size == 0) {
        size = FLASH_CONFIG_SIZE;
    }

    ETS_INTR_LOCK();
    SpiFlashOpResult result = spi_flash_read(FLASH_CONFIG_ADDR + offs, (uint32 *) data, size);
    ETS_INTR_UNLOCK();
    if (result != SPI_FLASH_RESULT_OK) {
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
    SpiFlashOpResult result;

    for (i = 0; i < FLASH_CONFIG_SIZE / SPI_FLASH_SEC_SIZE; i++) {
        sector = (FLASH_CONFIG_ADDR + SPI_FLASH_SEC_SIZE * i) / SPI_FLASH_SEC_SIZE;
        ETS_INTR_LOCK();
        result = spi_flash_erase_sector(sector);
        ETS_INTR_UNLOCK();
        if (result != SPI_FLASH_RESULT_OK) {
            DEBUG_FLASHCFG("failed to erase flash sector at 0x%05X",
                           FLASH_CONFIG_ADDR + SPI_FLASH_SEC_SIZE * i);
            return FALSE;
        }
        else {
            DEBUG_FLASHCFG("flash sector at 0x%05X erased",
                           FLASH_CONFIG_ADDR + SPI_FLASH_SEC_SIZE * i);
        }
    }

    ETS_INTR_LOCK();
    result = spi_flash_write(FLASH_CONFIG_ADDR, (uint32 *) data, FLASH_CONFIG_SIZE);
    ETS_INTR_UNLOCK();
    if (result != SPI_FLASH_RESULT_OK) {
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

bool flashcfg_reset(void) {
    DEBUG_FLASHCFG("resetting to factory defaults");

    uint8 *config_data = zalloc(FLASH_CONFIG_SIZE);
    bool result = flashcfg_save(config_data);
    free(config_data);

    return result;
}
