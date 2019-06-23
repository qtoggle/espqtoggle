
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
 *
 * sha1 stuff copied from https://github.com/esp8266/Arduino
 * sha256 stuff copied from https://github.com/B-Con/crypto-algorithms
 * hmac_sha256 stuff copied from https://github.com/cantora/avr-crypto-lib
 * base64 stuff copied from https://github.com/littlstar/b64.c
 */


#ifndef _ESPGOODIES_CRYPTO_H
#define _ESPGOODIES_CRYPTO_H


#include <c_types.h>


#define SHA1_LEN                20
#define SHA1_HEX_LEN            40
#define SHA256_LEN              32
#define SHA256_HEX_LEN          64


    /* result must be freed after using the following functions */

ICACHE_FLASH_ATTR uint8       * sha1(uint8 *data, int len);
ICACHE_FLASH_ATTR char        * sha1_hex(char *s);
ICACHE_FLASH_ATTR uint8       * sha256(uint8 *data, int len);
ICACHE_FLASH_ATTR char        * sha256_hex(char *s);

ICACHE_FLASH_ATTR uint8       * hmac_sha256(uint8 *data, int data_len, uint8 *key, int key_len);
ICACHE_FLASH_ATTR char        * hmac_sha256_hex(char *msg, char *key);


    /* URL & filename safe base64 */

ICACHE_FLASH_ATTR char        * b64_encode(uint8 *data, int len, bool padding);
ICACHE_FLASH_ATTR uint8       * b64_decode(char *s);

ICACHE_FLASH_ATTR char        * bin2hex(uint8 *bin, int len);
ICACHE_FLASH_ATTR uint8       * hex2bin(char *hex);


#endif /* _ESPGOODIES_CRYPTO_H */
