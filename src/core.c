
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

#include "espgoodies/common.h"
#include "espgoodies/wifi.h"
#include "espgoodies/system.h"
#include "espgoodies/utils.h"

#ifdef _SLEEP
#include "espgoodies/sleep.h"
#endif

#ifdef _OTA
#include "espgoodies/ota.h"
#endif

#include "common.h"
#include "config.h"
#include "device.h"
#include "core.h"


#define TASK_QUEUE_SIZE             8

#define TASK_POLL_PORTS             0x02
#define TASK_LISTEN_RESPOND         0x03
#define TASK_UPDATE_SYSTEM          0x04

#define MAX_EXPRESSION_DEPS_DEPTH   4
#define PORT_SAVE_INTERVAL          5


static uint32                       last_expr_time = 0;
static uint32                       last_port_save_time = 0;

static uint64                       change_mask = -1;
static uint32                       change_reasons = 0; /* bit mask of 1 - expression eval, 0 - other reasons */
static uint32                       port_save_mask = 0;
static bool                         ports_polling_enabled = FALSE;

#ifdef _SLEEP
/* used to prevent more than one value-change per port when using sleep mode with short wakes */
static uint32                       value_change_trigger_mask = 0;
#endif

static os_event_t                   task_queue[TASK_QUEUE_SIZE];


ICACHE_FLASH_ATTR static void       core_task(os_event_t *e);
ICACHE_FLASH_ATTR static void       on_value_change(void);


void core_init(void) {
    system_os_task(core_task, USER_TASK_PRIO_0, task_queue, TASK_QUEUE_SIZE);
    system_os_post(USER_TASK_PRIO_0, TASK_UPDATE_SYSTEM, (os_param_t) NULL);
}

void core_listen_respond(session_t *session) {
    system_os_post(USER_TASK_PRIO_0, TASK_LISTEN_RESPOND, (os_param_t) session);
}

void core_enable_ports_polling(void) {
    if (ports_polling_enabled) {
        return;
    }

    DEBUG_CORE("enabling ports polling");
    ports_polling_enabled = TRUE;
    system_os_post(USER_TASK_PRIO_0, TASK_POLL_PORTS, (os_param_t) NULL);
}

void core_disable_ports_polling(void) {
    if (!ports_polling_enabled) {
        return;
    }

    DEBUG_CORE("disabling ports polling");
    ports_polling_enabled = FALSE;
}

void core_poll_ports(void) {
#ifdef _OTA
    /* prevent port polling and related logic during OTA */
    if (ota_current_state() == OTA_STATE_DOWNLOADING) {
        return;
    }
#endif

    double value;
    uint32 i, now = system_uptime();
    uint64 now_ms = system_uptime_us() / 1000;

    /* evaluate time-dependent expressions */
    if (now != last_expr_time) {
        last_expr_time = now;
        change_mask |= (1ULL << TIME_EXPR_DEP_BIT);
    }

    change_mask |= (1ULL << TIME_MS_EXPR_DEP_BIT);

    /* port saving routine */
    if (port_save_mask && (now - last_port_save_time > PORT_SAVE_INTERVAL)) {
        last_port_save_time = now;

        config_save();

        port_save_mask = 0;
    }

    /* determine changed ports */
    port_t **port = all_ports, *p;
    while ((p = *port++)) {
        if (!IS_ENABLED(p)) {
            continue;
        }

        if (system_setup_mode_led_gpio_no == p->slot && system_setup_mode_active()) {
            /* don't mess with the led while in setup mode */
            continue;
        }

        if (p->heart_beat && (now_ms - p->last_heart_beat_time >= p->heart_beat_interval)) {
            p->last_heart_beat_time = now_ms;
            p->heart_beat(p);
        }

        /* don't read value more often than indicated by sampling interval */
        if ((now_ms - p->last_sample_time < p->sampling_interval) && change_mask != -1) {
            continue;
        }

        value = p->read_value(p);
        p->last_sample_time = now_ms;
        if (IS_UNDEFINED(value)) {
            continue;
        }

        if (p->transform_read) {
            /* temporarily set the new value to the port,
             * so that the transform expression uses the newly read value */
            double prev_value = p->value;
            p->value = value;
            value = expr_eval(p->transform_read);
            p->value = prev_value;

            /* ignore invalid value yielded by expression */
            if (IS_UNDEFINED(value)) {
                continue;
            }
        }

        if (!IS_OUTPUT(p) && FILTER_TYPE(p)) {
            value = port_filter_apply(p, value);
        }

        if (IS_UNDEFINED(value)) { /* value could have become undefined after filtering */
            continue;
        }

        if (p->value != value) {
            p->changed = TRUE;
            if (IS_UNDEFINED(p->value)) {
                DEBUG_PORT(p, "detected value change: (undefined) -> %s", dtostr(value, -1));
            }
            else {
                DEBUG_PORT(p, "detected value change: %s -> %s", dtostr(p->value, -1), dtostr(value, -1));
            }
            p->value = value;
            change_mask |= 1ULL << p->slot;
        }
    }

    /* call on_value_change() multiple times to cover
     * multiple levels of port expression dependencies */
    for (i = 0; (i < MAX_EXPRESSION_DEPS_DEPTH) && change_mask; i++) {
        on_value_change();
    }

    /* reset all changed flags, as some of them might have not been covered by on_value_change() calls */
    port = all_ports;
    while ((p = *port++)) {
        p->changed = FALSE;
    }

    /* reset changed ports and reasons */
    change_mask = 0;
    change_reasons = 0;
}

void update_port_expression(port_t *port) {
    DEBUG_PORT(port, "updating expression");

    change_mask |= 1ULL << port->slot;
    change_reasons &= ~(1UL << port->slot);
    core_poll_ports();
}

void port_mark_for_saving(port_t *port) {
    DEBUG_PORT(port, "marking for saving");
    port_save_mask |= 1UL << port->slot;
}

void ensure_ports_saved(void) {
    if (port_save_mask) {
        DEBUG_CORE("ports need saving");
        last_port_save_time = system_uptime();
        config_save();
        port_save_mask = 0;
    }
    else {
        DEBUG_CORE("ports are all saved");
    }
}


void core_task(os_event_t *e) {
    switch (e->sig) {
        case TASK_POLL_PORTS: {
            if (ports_polling_enabled) {
                /* schedule next ports polling */
                system_os_post(USER_TASK_PRIO_0, TASK_POLL_PORTS, (os_param_t) NULL);
                core_poll_ports();
            }

            break;
        }

        case TASK_UPDATE_SYSTEM: {
            /* schedule next system update */
            system_os_post(USER_TASK_PRIO_0, TASK_UPDATE_SYSTEM, (os_param_t) NULL);
            system_setup_mode_update();
            system_connected_led_update();

            break;
        }

        case TASK_LISTEN_RESPOND: {
            session_t *session = (session_t *) e->par;

            /* between event_push and this task,
             * another listen request might have been received for this session,
             * which would have eaten up our queued events */
            if (session->queue_len) {
                session_respond(session);
            }

            break;
        }
    }
}

void on_value_change(void) {
    uint64 new_change_mask = 0;

    port_t **port = all_ports, *p;
    while ((p = *port++)) {
        if (!IS_ENABLED(p)) {
            continue;
        }

        bool port_needs_eval = (change_mask & p->change_dep_mask) ||     /* one of port dependencies has changed */
                               (change_mask & (1ULL << p->slot)) ||         /* the port itself has changed */
                               (change_mask == -1);                      /* forced expression reevaluation */

        if (port_needs_eval && (change_reasons & (1UL << p->slot)) && (p->change_dep_mask & (1ULL << p->slot))) {
            /* if port expression depends on port itself and it just changed because of expression eval,
             * prevent reevaluating its expression to avoid evaluation loops */
            port_needs_eval = FALSE;
        }

        if (p->expr && IS_OUTPUT(p) && port_needs_eval) {
            double value = expr_eval(p->expr);

            /* ignore invalid values yielded by expressions */
            if (IS_UNDEFINED(value)) {
                continue;
            }

            if (value != p->value) {
                DEBUG_PORT(p, "expression \"%s\" evaluated to %s", p->sexpr, dtostr(value, -1));
                if (p->type == PORT_TYPE_BOOLEAN) {
                    value = !!value;
                }

                if (port_set_value(p, value)) {
                    p->value = value;
                    p->changed = TRUE;
                    change_reasons |= 1UL << p->slot;
                    new_change_mask |= 1ULL << p->slot;
                }

                continue;
            }
        }

        if (!p->changed) {
            continue;
        }

        if (IS_PERSISTED(p)) {
            port_mark_for_saving(p);
        }

        p->changed = FALSE; /* handled */

        /* add a value change event */
#ifdef _SLEEP
        if (sleep_is_short_wake()) {
            if (!(value_change_trigger_mask & (1UL << p->slot))) {
                value_change_trigger_mask |= 1UL << p->slot;
            }
            else {
                DEBUG_PORT(p, "skipping value-change event due to short wake");
                continue;
            }
        }
#endif

        event_push_value_change(p);
    }

    change_mask = new_change_mask;
}
