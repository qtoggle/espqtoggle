
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

#define TASK_POLL                   0x02
#define TASK_LISTEN_RESPOND         0x03
#define TASK_UPDATE_SYSTEM          0x04

#define CONFIG_SAVE_INTERVAL        5  /* Seconds */


static uint32                       last_expr_time = 0;
static uint32                       last_config_save_time = 0;
static uint32                       now;
static uint64                       now_ms;
static uint64                       now_us;

static uint32                       force_eval_expressions_mask = 0;
static bool                         config_needs_saving = FALSE;
static uint32                       poll_started_time_ms = 0;
static bool                         polling_enabled = FALSE;

#ifdef _SLEEP
/* Used to prevent more than one value-change per port when using sleep mode with short wakes */
static uint32                       value_change_trigger_mask = 0;
#endif

static os_event_t                   task_queue[TASK_QUEUE_SIZE];


ICACHE_FLASH_ATTR static void       core_task(os_event_t *e);
ICACHE_FLASH_ATTR static void       handle_value_changes(uint64 change_mask, uint32 change_reasons_expression_mask);


void core_init(void) {
    system_os_task(core_task, USER_TASK_PRIO_0, task_queue, TASK_QUEUE_SIZE);
    system_os_post(USER_TASK_PRIO_0, TASK_UPDATE_SYSTEM, (os_param_t) NULL);
}

void core_listen_respond(session_t *session) {
    system_os_post(USER_TASK_PRIO_0, TASK_LISTEN_RESPOND, (os_param_t) session);
}

void core_enable_polling(void) {
    if (polling_enabled) {
        return;
    }

    DEBUG_CORE("enabling polling");
    polling_enabled = TRUE;
    system_os_post(USER_TASK_PRIO_0, TASK_POLL, (os_param_t) NULL);
}

void core_disable_polling(void) {
    if (!polling_enabled) {
        return;
    }

    DEBUG_CORE("disabling polling");
    polling_enabled = FALSE;
}

void core_poll(void) {
#ifdef _OTA
    /* Prevent port polling and related logic during OTA */
    if (ota_current_state() == OTA_STATE_DOWNLOADING) {
        return;
    }
#endif

    static uint64 change_mask = -1;
    uint32 change_reasons_expression_mask = 0;

    double value;
    now_us = system_uptime_us();
    now_ms = now_us / 1000;
    now = now_ms / 1000;

    /* Add time dependency masks */
    if (now != last_expr_time) {
        last_expr_time = now;
        change_mask |= (1ULL << TIME_EXPR_DEP_BIT);
    }

    if (poll_started_time_ms == 0) {
        poll_started_time_ms = now_ms;
    }

    change_mask |= (1ULL << TIME_MS_EXPR_DEP_BIT);

    if (config_needs_saving && ((int64) now - last_config_save_time > CONFIG_SAVE_INTERVAL)) {
        last_config_save_time = now;
        config_needs_saving = FALSE;
        config_save();
    }

    /* Determine changed ports */
    port_t **port = all_ports, *p;
    while ((p = *port++)) {
        if (!IS_ENABLED(p)) {
            continue;
        }

        /* Don't mess with the led while in setup mode */
        if ((system_setup_mode_led_gpio_no == p->slot) && system_setup_mode_active()) {
            continue;
        }

        if (p->heart_beat && (now_ms - p->last_heart_beat_time >= p->heart_beat_interval)) {
            p->last_heart_beat_time = now_ms;
            p->heart_beat(p);
        }

        /* Don't read value more often than indicated by sampling interval */
        if (now_ms - p->last_sample_time < p->sampling_interval) {
            continue;
        }

        value = p->read_value(p);
        p->last_sample_time = now_ms;
        if (IS_UNDEFINED(value)) {
            continue;
        }

        if (p->transform_read) {
            /* Temporarily set the new value to the port, so that the transform expression uses the newly read value */
            double prev_value = p->value;
            p->value = value;
            value = expr_eval(p->transform_read);
            p->value = prev_value;

            /* Ignore invalid value yielded by expression */
            if (IS_UNDEFINED(value)) {
                continue;
            }
        }

        if (p->value != value) {
            if (IS_UNDEFINED(p->value)) {
                DEBUG_PORT(p, "detected value change: (undefined) -> %s, reason = %c",
                           dtostr(value, -1), p->change_reason);
            }
            else {
                DEBUG_PORT(p, "detected value change: %s -> %s, reason = %c",
                           dtostr(p->value, -1), dtostr(value, -1), p->change_reason);
            }

            p->value = value;
            change_mask |= 1ULL << p->slot;

            /* Remember and reset change reason */
            if (p->change_reason == CHANGE_REASON_EXPRESSION) {
                change_reasons_expression_mask |= 1UL << p->slot;
            }
            p->change_reason = CHANGE_REASON_NATIVE;
        }
    }

    if (change_mask || force_eval_expressions_mask) {
        handle_value_changes(change_mask, change_reasons_expression_mask);
    }

    change_mask = 0;
}

void update_port_expression(port_t *port) {
    DEBUG_PORT(port, "updating expression");

    force_eval_expressions_mask |= 1UL << port->slot;
    port->change_reason = CHANGE_REASON_NATIVE;
}

void config_mark_for_saving(void) {
    DEBUG_CORE("marking config for saving");
    config_needs_saving = TRUE;
}

void config_ensure_saved(void) {
    if (config_needs_saving) {
        DEBUG_CORE("config needs saving");
        last_config_save_time = system_uptime();
        config_needs_saving = FALSE;
        config_save();
    }
}


void core_task(os_event_t *e) {
    switch (e->sig) {
        case TASK_POLL: {
            if (polling_enabled) {
                /* Schedule next polling */
                system_os_post(USER_TASK_PRIO_0, TASK_POLL, (os_param_t) NULL);
                core_poll();
            }

            break;
        }

        case TASK_UPDATE_SYSTEM: {
            /* Schedule next system update */
            system_os_post(USER_TASK_PRIO_0, TASK_UPDATE_SYSTEM, (os_param_t) NULL);
            system_setup_mode_update();
            system_connected_led_update();

            break;
        }

        case TASK_LISTEN_RESPOND: {
            session_t *session = (session_t *) e->par;

            /* Between event_push and this task, another listen request might have been received for this session,
             * which would have eaten up our queued events */
            if (session->queue_len) {
                session_respond(session);
            }

            break;
        }
    }
}

void handle_value_changes(uint64 change_mask, uint32 change_reasons_expression_mask) {
    /* Also consider ports whose expressions were marked for forced evaluation */
    change_mask |= force_eval_expressions_mask;
    force_eval_expressions_mask = 0;

    /* Trigger value-change events; save persisted ports */
    port_t **port = all_ports, *p;
    while ((p = *port++)) {
        if (!((1ULL << p->slot) & change_mask)) {
            continue;
        }

        /* Add a value change event */
#ifdef _SLEEP
        if (sleep_is_short_wake()) {
            if (!(value_change_trigger_mask & (1UL << p->slot))) {
                value_change_trigger_mask |= 1UL << p->slot;
                event_push_value_change(p);
            }
            else {
                DEBUG_PORT(p, "skipping value-change event due to short wake");
            }
        }
#else
        event_push_value_change(p);
#endif

        if (IS_PERSISTED(p) && (now_ms - poll_started_time_ms > 2000)) {
            /* Don't save config during the first few seconds since polling starts; this avoids saving at each boot due
             * to port values transitioning from undefined to their initial value */
            config_mark_for_saving();
        }
    }

    /* Reevaluate the expressions depending on changed ports */
    port = all_ports;
    while ((p = *port++)) {
        if (!IS_ENABLED(p)) {
            continue;
        }

        /* Only output (writable) ports have value expressions */
        if (!IS_OUTPUT(p)) {
            continue;
        }

        if (!p->expr) {
            continue;
        }

        /* If port expression depends on port itself and the change reason is the evaluation of its expression, prevent
         * evaluating its expression again to avoid evaluation loops */
        if ((change_mask & (1ULL << p->slot)) &&
            (change_reasons_expression_mask & (1UL << p->slot)) &&
            (p->change_dep_mask & (1ULL << p->slot))) {
            continue;
        }

        /* Evaluate a port's expression when:
         * - one of its deps changed
         * - its own value changed */

        if (!(change_mask & p->change_dep_mask) && !(change_mask & (1ULL << p->slot))) {
            continue;
        }

        double value = expr_eval(p->expr);
        if (IS_UNDEFINED(value)) {
            continue;
        }

        /* Adapt boolean type value */
        if (p->type == PORT_TYPE_BOOLEAN) {
            value = !!value;
        }

        /* Check if value has changed after expression evaluation */
        if (value == p->value) {
            continue;
        }

        DEBUG_PORT(p, "expression \"%s\" evaluated to %s", p->sexpr, dtostr(value, -1));

        port_set_value(p, value, CHANGE_REASON_EXPRESSION);
    }
}
