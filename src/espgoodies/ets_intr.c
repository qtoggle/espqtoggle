
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

/*
 * Adds support for nested (reentrant) interrupt locks.
 * Taken from Arduino: https://github.com/esp8266/Arduino/pull/6484
 */


#include <c_types.h>
#include <ets_sys.h>


#define __STRINGIFY(a)                  #a
#define ETS_INTR_LOCK_NEST_MAX          7
static uint16                           ets_intr_lock_stack[ETS_INTR_LOCK_NEST_MAX];
static uint8                            ets_intr_lock_stack_ptr = 0;

#define xt_wsr_ps(state)                __asm__ __volatile__("wsr %0,ps; isync" :: "a" (state) : "memory")
#define xt_rsil(level)                  (__extension__({                                            \
                                            uint32_t state;                                         \
                                            __asm__ __volatile__("rsil %0," __STRINGIFY(level) :    \
                                                                 "=a" (state) ::                    \
                                                                 "memory");                         \
                                            state;                                                  \
                                        }))


void                                    ets_intr_lock();
void                                    ets_intr_unlock();
bool                                    ets_post(uint8 prio, ETSSignal sig, ETSParam par);
bool                                    ets_post_rom(uint8 prio, ETSSignal sig, ETSParam par);


void ets_intr_lock() {
    if (ets_intr_lock_stack_ptr < ETS_INTR_LOCK_NEST_MAX) {
        ets_intr_lock_stack[ets_intr_lock_stack_ptr++] = xt_rsil(3);
    }
    else {
        xt_rsil(3);
    }
}

void ets_intr_unlock() {
    if (ets_intr_lock_stack_ptr > 0) {
        xt_wsr_ps(ets_intr_lock_stack[--ets_intr_lock_stack_ptr]);
    }
    else {
        xt_rsil(0);
    }
}


bool ets_post(uint8 prio, ETSSignal sig, ETSParam par) {
    /* Save/restore the PS state across the ROM ets_post() call as the ROM code does not implement this correctly */

    uint32 saved;
    asm volatile ("rsr %0,ps" : "=a" (saved));
    bool rc = ets_post_rom(prio, sig, par);
    xt_wsr_ps(saved);
    return rc;
}
