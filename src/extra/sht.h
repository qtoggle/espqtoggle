
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
 */

#ifndef _EXTRA_SHT_H
#define _EXTRA_SHT_H

#include <c_types.h>


#ifdef _DEBUG_SHT
#define DEBUG_SHT(f, ...)                   DEBUG("[sht           ] " f, ##__VA_ARGS__)
#else
#define DEBUG_SHT(...)                      {}
#endif

ICACHE_FLASH_ATTR void                      sht_init_ports(void);


#endif /* _EXTRA_SHT_H */
