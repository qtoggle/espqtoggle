
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
 * Inspired from https://github.com/esp8266/Arduino/blob/master/libraries/SPI/SPI.h
 */

#ifndef _HSPI_H
#define _HSPI_H


#include <c_types.h>


#define HSPI_BIT_ORDER_MSB_FIRST 0
#define HSPI_BIT_ORDER_LSB_FIRST 1

#ifdef _DEBUG_HSPI
#define DEBUG_HSPI(fmt, ...) DEBUG("[hspi          ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_HSPI(...)      {}
#endif


ICACHE_FLASH_ATTR void  hspi_setup(uint8 bit_order, bool cpol, bool cpha, uint32 freq);
ICACHE_FLASH_ATTR bool  hspi_get_current_setup(uint8 *bit_order, bool *cpol, bool *cpha, uint32 *freq);
ICACHE_FLASH_ATTR void  hspi_transfer(uint8 *out_buff, uint8 *in_buff, uint32 len);
ICACHE_FLASH_ATTR uint8 hspi_transfer_byte(uint8 byte);


#endif /* _HSPI_H */
