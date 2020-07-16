
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

#ifndef _ESPGOODIES_HTML_H
#define _ESPGOODIES_HTML_H

#include <c_types.h>


#ifdef _DEBUG_HTML
#define DEBUG_HTML(fmt, ...) DEBUG("[html          ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_HTML(...)      {}
#endif


/* Result must be freed */
uint8 ICACHE_FLASH_ATTR *html_load(uint32 *len);


#endif /* _ESPGOODIES_HTML_H */
