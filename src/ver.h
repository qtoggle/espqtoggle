
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

#ifndef _VERSION_H
#define _VERSION_H


#define API_VERSION             "1.0"
#define FW_VERSION              "0.9.0b11"

#define version_is_beta()       ((FW_VERSION[strlen(FW_VERSION) - 2] == 'b') || \
                                 (FW_VERSION[strlen(FW_VERSION) - 3] == 'b'))

#define version_is_alpha()      ((FW_VERSION[strlen(FW_VERSION) - 2] == 'a') || \
                                 (FW_VERSION[strlen(FW_VERSION) - 3] == 'a'))


#endif /* _VERSION_H */
