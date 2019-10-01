
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

#ifndef _ESPGOODIES_SLEEP_H
#define _ESPGOODIES_SLEEP_H


#include <c_types.h>


#ifdef _DEBUG_SLEEP
#define DEBUG_SLEEP(fmt, ...)           DEBUG("[sleep         ] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_SLEEP(...)                {}
#endif

#define SLEEP_WAKE_INTERVAL_MIN         0           /* 0 - sleep disabled */
#define SLEEP_WAKE_INTERVAL_MAX         (7 * 1440)  /* minutes */
#define SLEEP_WAKE_DURATION_MIN         1           /* seconds */
#define SLEEP_WAKE_DURATION_MAX         3600        /* seconds */
#define SLEEP_WAKE_CONNECT_TIMEOUT      20          /* seconds */


ICACHE_FLASH_ATTR void                  sleep_init(void);
ICACHE_FLASH_ATTR void                  sleep_reset(void);
ICACHE_FLASH_ATTR bool                  sleep_pending(void);
ICACHE_FLASH_ATTR bool                  sleep_is_short_wake(void);

ICACHE_FLASH_ATTR int                   sleep_get_wake_interval(void);
ICACHE_FLASH_ATTR void                  sleep_set_wake_interval(int interval);

ICACHE_FLASH_ATTR int                   sleep_get_wake_duration(void);
ICACHE_FLASH_ATTR void                  sleep_set_wake_duration(int duration);


#endif /* _ESPGOODIES_SLEEP_H */
