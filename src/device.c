
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

#include "espgoodies/crypto.h"

#include "api.h"
#include "device.h"


char                    device_hostname[API_MAX_DEVICE_NAME_LEN + 1] = {0};
char                    device_description[API_MAX_DEVICE_DESC_LEN + 1] = {0};
char                    device_admin_password_hash[SHA256_HEX_LEN + 1] = {0};
char                    device_normal_password_hash[SHA256_HEX_LEN + 1] = {0};
char                    device_viewonly_password_hash[SHA256_HEX_LEN + 1] = {0};
uint32                  device_flags = 0;
uint16                  device_tcp_port = 0;
