
/*
 * Copyright (c) Calin Crisan
 * This file is part of espqToggle.
 *
 * espqToggle is free software: you can redistribute it and/or modify
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

/*
 * Inspired from https://github.com/PaulStoffregen/OneWire/
 */

#include <gpio.h>
#include <user_interface.h>

#include "espgoodies/common.h"
#include "espgoodies/utils.h"

#include "onewire.h"


ICACHE_FLASH_ATTR static void       write_bit(one_wire_t *one_wire, bool value);
ICACHE_FLASH_ATTR static bool       read_bit(one_wire_t *one_wire);


bool one_wire_reset(one_wire_t *one_wire) {
    ETS_INTR_LOCK();

    GPIO_OUTPUT_SET(one_wire->gpio_no, 0);
    os_delay_us(500);

    GPIO_DIS_OUTPUT(one_wire->gpio_no);
    os_delay_us(80);

    if (GPIO_INPUT_GET(one_wire->gpio_no)) {
        ETS_INTR_UNLOCK();
        return FALSE;
    }

    os_delay_us(300);

    if (!GPIO_INPUT_GET(one_wire->gpio_no)) {
        ETS_INTR_UNLOCK();
        return FALSE;
    }

    ETS_INTR_UNLOCK();
    return TRUE;
}

void one_wire_search_reset(one_wire_t *one_wire) {
    uint8 i;

    one_wire->last_discrepancy = 0;
    one_wire->last_device_flag = FALSE;
    one_wire->last_family_discrepancy = 0;

    for (i = 0; i < 8; i++) {
        one_wire->rom[i] = 0;
    }
}

bool one_wire_search(one_wire_t *one_wire, uint8 *addr) {
    uint8 i;
    uint8 id_bit_number = 1;
    uint8 last_zero = 0;
    uint8 rom_byte_number = 0;
    uint8 rom_byte_mask = 1;
    uint8 search_direction;
    bool id_bit, cmp_id_bit;
    bool search_result = FALSE;

    /* if the last call was not the very last one */
    if (!one_wire->last_device_flag) {
        if (!one_wire_reset(one_wire)) {
            one_wire->last_discrepancy = 0;
            one_wire->last_device_flag = FALSE;
            one_wire->last_family_discrepancy = 0;

            return FALSE;
        }

        one_wire_write(one_wire, ONE_WIRE_CMD_SEARCH_ROM, /* parasitic = */ FALSE);

        /* search loop */
        do {
            id_bit = read_bit(one_wire);
            cmp_id_bit = read_bit(one_wire);

            /* check for no devices on 1-wire */
            if (id_bit && cmp_id_bit) {
                break;
            }

            if (id_bit != cmp_id_bit) {
                search_direction = id_bit;  /* bit write value for search */
            }
            else {
                if (id_bit_number < one_wire->last_discrepancy) {
                    search_direction = (one_wire->rom[rom_byte_number] & rom_byte_mask) > 0;
                }
                else {
                    search_direction = id_bit_number == one_wire->last_discrepancy;
                }

                if (!search_direction) {
                    last_zero = id_bit_number;

                    /* check for last discrepancy in family */
                    if (last_zero < 9) {
                        one_wire->last_family_discrepancy = last_zero;
                    }
                }
            }

            /* set or clear the bit in the ROM byte rom_byte_number with mask rom_byte_mask */
            if (search_direction) {
                one_wire->rom[rom_byte_number] |= rom_byte_mask;
            }
            else {
                one_wire->rom[rom_byte_number] &= ~rom_byte_mask;
            }

            /* serial number search direction write bit */
            write_bit(one_wire, search_direction);

            /* increment the byte counter id_bit_number and shift the mask rom_byte_mask */
            id_bit_number++;
            rom_byte_mask <<= 1;

            /* if the mask is 0 then go to new SerialNum byte rom_byte_number and reset mask */
            if (rom_byte_mask == 0) {
                rom_byte_number++;
                rom_byte_mask = 1;
            }

            system_soft_wdt_feed();

        } while (rom_byte_number < 8);  /* loop through all ROM bytes */

        if (id_bit_number >= 65) {  /* the search was successful */
            one_wire->last_discrepancy = last_zero;

            /* check for last device */
            if (one_wire->last_discrepancy == 0) {
                one_wire->last_device_flag = TRUE;
            }

            search_result = TRUE;
        }
    }

    /* if no device found, reset state so next search will be a first */
    if (!search_result || !one_wire->rom[0]) {
        one_wire->last_discrepancy = 0;
        one_wire->last_family_discrepancy = 0;
        one_wire->last_device_flag = FALSE;
        search_result = FALSE;
    }
    else {
        for (i = 0; i < 8; i++) {
            addr[i] = one_wire->rom[i];
        }
    }

    return search_result;
}

uint8 one_wire_read(one_wire_t *one_wire) {
    uint8 mask;
    uint8 result = 0;

    for (mask = 0x01; mask; mask <<= 1) {
        if (read_bit(one_wire)) {
            result |= mask;
        }
    }

    return result;
}

void one_wire_write(one_wire_t *one_wire, uint8 value, bool parasitic) {
    uint8 mask;

    for (mask = 0x01; mask; mask <<= 1) {
        write_bit(one_wire, !!(mask & value));
    }

    if (parasitic) {
        GPIO_DIS_OUTPUT(one_wire->gpio_no);
    }
    else {
        GPIO_OUTPUT_SET(one_wire->gpio_no, 1);
    }
}

void one_wire_write_bytes(one_wire_t *one_wire, uint8 *buf, uint16 len, bool parasitic) {
    uint16 i;
    for (i = 0 ;i < len; i++) {
        one_wire_write(one_wire, buf[i], /* parasitic = */ FALSE);
    }

    if (parasitic) {
        GPIO_DIS_OUTPUT(one_wire->gpio_no);
    }
    else {
        GPIO_OUTPUT_SET(one_wire->gpio_no, 1);
    }
}

void one_wire_parasitic_power_off(one_wire_t *one_wire) {
    /* set input to floating */
    GPIO_DIS_OUTPUT(one_wire->gpio_no);
}

uint8 one_wire_crc8(uint8 *buf, int len) {
    uint8 crc = 0;
    uint8 i, mix;

    while (len--) {
        uint8 inbyte = *buf++;
        for (i = 8; i; i--) {
            mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if (mix) {
                crc ^= 0x8C;
            }
            inbyte >>= 1;
        }
    }

    return crc;
}


void write_bit(one_wire_t *one_wire, bool bit) {
    ETS_INTR_LOCK();
    os_delay_us(1);

    GPIO_OUTPUT_SET(one_wire->gpio_no, 0);

    if (bit) {
        os_delay_us(5);
        GPIO_DIS_OUTPUT(one_wire->gpio_no);
        os_delay_us(55);
        ETS_INTR_UNLOCK();
    }
    else {
        os_delay_us(55);
        GPIO_DIS_OUTPUT(one_wire->gpio_no);
        os_delay_us(5);
        ETS_INTR_UNLOCK();
    }
}

bool read_bit(one_wire_t *one_wire) {
    bool result;

    ETS_INTR_LOCK();
    os_delay_us(1);

    GPIO_OUTPUT_SET(one_wire->gpio_no, 0);
    os_delay_us(15);

    GPIO_DIS_OUTPUT(one_wire->gpio_no);
    os_delay_us(5);

    result = GPIO_INPUT_GET(one_wire->gpio_no);

    os_delay_us(40);
    ETS_INTR_UNLOCK();

    return result;
}
