
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


#include <user_interface.h>
#include <mem.h>

#include "espgoodies/common.h"

#include "peri.h"


peri_class_t all_peri_classes[] = {

};
peri_t **all_peri = NULL;


//ICACHE_FLASH_ATTR static void   event_push(int type, char *port_id);


peri_class_t *peri_class_lookup(char *name) {
    return NULL;
}

void peri_register(peri_t *peri) {

}

void peri_unregister_all(void) {

}
