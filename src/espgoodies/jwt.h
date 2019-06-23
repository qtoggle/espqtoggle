
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

#ifndef _ESPGOODIES_JWT_H
#define _ESPGOODIES_JWT_H


#include <c_types.h>

#include "json.h"


#define JWT_ALG_HS256           1


typedef struct {

    uint8           alg;
    json_t        * claims;

} jwt_t;


ICACHE_FLASH_ATTR jwt_t       * jwt_new(uint8 alg, json_t *claims);
ICACHE_FLASH_ATTR void          jwt_free(jwt_t *jwt);

ICACHE_FLASH_ATTR char        * jwt_dump(jwt_t *jwt, char *secret);
ICACHE_FLASH_ATTR jwt_t       * jwt_parse(char *jwt_str);
ICACHE_FLASH_ATTR bool          jwt_validate(char *jwt_str, uint8 alg, char *secret);


#endif /* _ESPGOODIES_JWT_H */
