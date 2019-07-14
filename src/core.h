
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

#ifndef _CORE_H
#define _CORE_H


#include <c_types.h>

#include "sessions.h"


ICACHE_FLASH_ATTR void                  core_init(void);
ICACHE_FLASH_ATTR void                  core_listen_respond(session_t *session);

ICACHE_FLASH_ATTR void                  force_expr_eval(void);
ICACHE_FLASH_ATTR void                  port_mark_for_saving(port_t *port);
ICACHE_FLASH_ATTR void                  ensure_ports_saved(void);


#endif /* _CORE_H */
