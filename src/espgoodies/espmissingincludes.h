
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

#ifndef _ESPGOODIES_ESPMISSINGINCLUDES_H
#define _ESPGOODIES_ESPMISSINGINCLUDES_H

#include <stdarg.h>
#include <ets_sys.h>

void ets_isr_mask(unsigned intr);
void ets_isr_unmask(unsigned intr);
int ets_sprintf(char *str, const char *format, ...)  __attribute__ ((format (printf, 2, 3)));
int ets_snprintf(char *str, size_t size, const char *format, ...) __attribute__ ((format (printf, 3, 4)));
int ets_vsnprintf(char *str, size_t size, const char *format, va_list args) __attribute__ ((format (printf, 3, 0)));

#define snprintf    ets_snprintf
#define vsnprintf   ets_vsnprintf


typedef signed short        int16;
typedef signed long long    int64;

#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif

#endif /* _ESPGOODIES_ESPMISSINGINCLUDES_H */
