
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

#ifndef _EXTRA_DHT_H
#define _EXTRA_DHT_H

#include <ets_sys.h>

#include "ports.h"


#ifdef _DEBUG_DHT
#define DEBUG_DHT(port, f, ...)     DEBUG("[dht.%-10s] " f, (port)->id, ##__VA_ARGS__)
#else
#define DEBUG_DHT(...)              {}
#endif

ICACHE_FLASH_ATTR void              dht_init_ports(void);


#endif /* _EXTRA_DHT_H */
