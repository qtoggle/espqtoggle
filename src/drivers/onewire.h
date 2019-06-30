
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

#ifndef _ONE_WIRE_H
#define _ONE_WIRE_H


#include <c_types.h>


#define ONE_WIRE_CMD_SEARCH_ROM         0xF0
#define ONE_WIRE_CMD_SKIP_ROM           0xCC
#define ONE_WIRE_CMD_CONVERT_T          0x44
#define ONE_WIRE_CMD_READ_SCRATCHPAD    0xBE


typedef struct {

    uint8                               gpio_no;
    uint8                               rom[8];
    uint8                               last_discrepancy;
    uint8                               last_family_discrepancy;
    bool                                last_device_flag;

} one_wire_t;


ICACHE_FLASH_ATTR void                  one_wire_setup(one_wire_t *one_wire);
ICACHE_FLASH_ATTR bool                  one_wire_reset(one_wire_t *one_wire);

ICACHE_FLASH_ATTR void                  one_wire_search_reset(one_wire_t *one_wire);
ICACHE_FLASH_ATTR bool                  one_wire_search(one_wire_t *one_wire, uint8 *addr);

ICACHE_FLASH_ATTR uint8                 one_wire_read(one_wire_t *one_wire);
ICACHE_FLASH_ATTR void                  one_wire_write(one_wire_t *one_wire, uint8 value, bool parasitic);
ICACHE_FLASH_ATTR void                  one_wire_write_bytes(one_wire_t *one_wire, uint8 *buf, uint16 len, bool parasitic);

ICACHE_FLASH_ATTR void                  one_wire_parasitic_power_off(one_wire_t *one_wire);

ICACHE_FLASH_ATTR uint8                 one_wire_crc8(uint8 *buf, int len);


#endif /* _ONE_WIRE_H */
