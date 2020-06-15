
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

#ifndef _PERI_GPIO_H
#define _PERI_GPIO_H

#include <c_types.h>

#include "peri.h"


#ifdef _DEBUG_PERI_GPIO
#define DEBUG_PERI_GPIO(port, f, ...)   DEBUG("[%-14s] " f, (port)->id, ##__VA_ARGS__)
#else
#define DEBUG_PERI_GPIO(...)            {}
#endif


extern peri_class_t                     peri_class_gpio;


#endif /* _PERI_GPIO_H */
