
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

#ifndef _ESPGOODIES_GPIO_H
#define _ESPGOODIES_GPIO_H


#include <os_type.h>
#include <ctype.h>


#ifdef _DEBUG_GPIO
#define DEBUG_GPIO(fmt, ...)    DEBUG("[gpio          ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_GPIO(...)         {}
#endif


int                             gpio_get_mux(int gpio_no);
int                             gpio_get_func(int gpio_no);
void                            gpio_select_func(int gpio_no);
void                            gpio_set_pull(int gpio_no, bool pull);

void                            gpio_configure_input(int gpio_no, bool pull);
void                            gpio_configure_output(int gpio_no, bool initial);

bool                            gpio_is_configured(int gpio_no);
bool                            gpio_is_output(int gpio_no);
bool                            gpio_get_pull(int gpio_no);

void                            gpio_write_value(int gpio_no, bool value);
bool                            gpio_read_value(int gpio_no);


#endif /* _ESPGOODIES_GPIO_H */
