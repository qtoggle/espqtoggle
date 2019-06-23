
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

#ifndef _ESPGOODIES_HTTPUTILS_H
#define _ESPGOODIES_HTTPUTILS_H


#include "json.h"


ICACHE_FLASH_ATTR json_t          * http_parse_url_encoded(char *input);
ICACHE_FLASH_ATTR char            * http_build_auth_token_header(char *token);
ICACHE_FLASH_ATTR char            * http_parse_auth_token_header(char *header);


#endif /* _ESPGOODIES_HTTPUTILS_H */
