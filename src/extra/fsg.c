
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

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <mem.h>
#include <gpio.h>
#include <user_interface.h>

#include "espgoodies/common.h"
#include "espgoodies/utils.h"
#include "espgoodies/system.h"

#include "ports.h"
#include "extra/fsg.h"


#ifdef HAS_FSG


#define STATE_CLOSING                  -2
#define STATE_CLOSED                   -1
#define STATE_STOPPED                   0  /* not used in the state machine */
#define STATE_OPENED                    1
#define STATE_OPENING                   2
#define STATE_OPENED_WILL_OPEN          3
#define STATE_OPENED_WILL_CLOSE         4

#define CMD_CLOSE                      -1
#define CMD_STOP                        0
#define CMD_OPEN                        1

#define INPUT_CHOICES_LEN               4

#define BOOT_DELAY                      2000    /* milliseconds */
#define HEART_BEAT_INTERVAL             20      /* milliseconds */
#define INPUT_DEBOUNCE_HIST_LEN         100     /* up to 2s, with heart beat 20ms */
#define INPUT_DELAY_HIST_LEN            250     /* up to 5s, with heart beat 20ms */

#define DEBOUNCE_DURATION_MIN          -(INPUT_DEBOUNCE_HIST_LEN * HEART_BEAT_INTERVAL)  /* milliseconds */
#define DEBOUNCE_DURATION_MAX           (INPUT_DEBOUNCE_HIST_LEN * HEART_BEAT_INTERVAL)  /* milliseconds */

#define CLOSED_MOVING_DELAY_MIN        -(INPUT_DELAY_HIST_LEN * HEART_BEAT_INTERVAL)  /* milliseconds */
#define CLOSED_MOVING_DELAY_MAX         (INPUT_DELAY_HIST_LEN * HEART_BEAT_INTERVAL)  /* milliseconds */

#define OUTPUT_CHOICES_LEN              3

#define PULSE_LEN_MIN                   (10 * HEART_BEAT_INTERVAL)      /* milliseconds */
#define PULSE_LEN_MAX                   (10000 * HEART_BEAT_INTERVAL)   /* milliseconds */
#define PULSE_LEN_DEF                   1000                            /* milliseconds */

#define CLOSED_INPUT_CONFIG_OFFS        0x00    /* 1 byte */
#define MOVING_INPUT_CONFIG_OFFS        0x01    /* 1 byte */
#define OPEN_OUTPUT_CONFIG_OFFS         0x02    /* 1 byte */
#define CLOSE_OUTPUT_CONFIG_OFFS        0x03    /* 1 byte */
#define STOP_OUTPUT_CONFIG_OFFS         0x04    /* 1 byte */
                                                /* 3 bytes - reserved */
#define PULSE_ON_LEN_CONFIG_OFFS        0x08    /* 2 bytes */
#define PULSE_OFF_LEN_CONFIG_OFFS       0x0A    /* 2 bytes */
#define DEBOUNCE_DURATION_CONFIG_OFFS   0x0C    /* 2 bytes */
#define CLOSED_MOVING_DELAY_CONFIG_OFFS 0x0E    /* 2 bytes */

#define FLAG_CLOSED_INPUT_LEVEL         0x01000000
#define FLAG_MOVING_INPUT_LEVEL         0x02000000
#define FLAG_OUTPUT_LEVEL               0x04000000

#define ACTIVE_HIGH                     1
#define ACTIVE_LOW                      0

#define PENDING_OUTPUTS_MAX_LEN         16

#define OUTPUT_STATE_IDLE               0
#define OUTPUT_STATE_ON                 1
#define OUTPUT_STATE_OFF                2

#define CLOSED_INPUT_STATE_MASK         0x01
#define MOVING_INPUT_STATE_MASK         0x02


typedef struct {

    int8                                state;
    uint8                               last_input_state;

    int8                                closed_input;
    int8                                moving_input;

    int32                               debounce_duration;
    int32                               closed_moving_delay;

    int8                                open_output;
    int8                                close_output;
    int8                                stop_output;

    uint32                              output_pulse_on_len;
    uint32                              output_pulse_off_len;

    uint8                             * pending_outputs;
    uint8                               pending_outputs_len;

    int8                                current_output_state;
    int8                                current_output;
    int64                               current_output_rem;

    uint64                              last_heart_beat_time;

    /* used for input debouncing */
    uint8                               closed_input_debounce_hist[INPUT_DEBOUNCE_HIST_LEN];
    uint8                               moving_input_debounce_hist[INPUT_DEBOUNCE_HIST_LEN];

    /* used for input delay */
    uint8                               closed_input_delay_hist[INPUT_DELAY_HIST_LEN];
    uint8                               moving_input_delay_hist[INPUT_DELAY_HIST_LEN];

} extra_info_t;


#define get_state(p)                    (((extra_info_t *) (p)->extra_info)->state)
#define set_state(p, v)                 {((extra_info_t *) (p)->extra_info)->state = v;}

#define get_last_input_state(p)         (((extra_info_t *) (p)->extra_info)->last_input_state)
#define set_last_input_state(p, v)      {((extra_info_t *) (p)->extra_info)->last_input_state = v;}

#define get_closed_input(p)             (((extra_info_t *) (p)->extra_info)->closed_input)
#define set_closed_input(p, v)          {((extra_info_t *) (p)->extra_info)->closed_input = v;}

#define get_moving_input(p)             (((extra_info_t *) (p)->extra_info)->moving_input)
#define set_moving_input(p, v)          {((extra_info_t *) (p)->extra_info)->moving_input = v;}

#define get_closed_input_level(p)       (!!((p)->flags & FLAG_CLOSED_INPUT_LEVEL))
#define set_closed_input_level(p, v)    {if (v) (p)->flags |= FLAG_CLOSED_INPUT_LEVEL; \
                                         else (p)->flags &= ~FLAG_CLOSED_INPUT_LEVEL;}

#define get_moving_input_level(p)       (!!((p)->flags & FLAG_MOVING_INPUT_LEVEL))
#define set_moving_input_level(p, v)    {if (v) (p)->flags |= FLAG_MOVING_INPUT_LEVEL; \
                                         else (p)->flags &= ~FLAG_MOVING_INPUT_LEVEL;}

#define get_debounce_duration(p)        (((extra_info_t *) (p)->extra_info)->debounce_duration)
#define set_debounce_duration(p, v)     {((extra_info_t *) (p)->extra_info)->debounce_duration = v;}

#define get_closed_moving_delay(p)      (((extra_info_t *) (p)->extra_info)->closed_moving_delay)
#define set_closed_moving_delay(p, v)   {((extra_info_t *) (p)->extra_info)->closed_moving_delay = v;}

#define get_open_output(p)              (((extra_info_t *) (p)->extra_info)->open_output)
#define set_open_output(p, v)           {((extra_info_t *) (p)->extra_info)->open_output = v;}

#define get_close_output(p)             (((extra_info_t *) (p)->extra_info)->close_output)
#define set_close_output(p, v)          {((extra_info_t *) (p)->extra_info)->close_output = v;}

#define get_stop_output(p)              (((extra_info_t *) (p)->extra_info)->stop_output)
#define set_stop_output(p, v)           {((extra_info_t *) (p)->extra_info)->stop_output = v;}

#define get_output_level(p)             (!!((p)->flags & FLAG_OUTPUT_LEVEL))
#define set_output_level(p, v)          {if (v) (p)->flags |= FLAG_OUTPUT_LEVEL; \
                                         else (p)->flags &= ~FLAG_OUTPUT_LEVEL;}

#define get_output_pulse_on_len(p)      (((extra_info_t *) (p)->extra_info)->output_pulse_on_len)
#define set_output_pulse_on_len(p, v)   {((extra_info_t *) (p)->extra_info)->output_pulse_on_len = v;}

#define get_output_pulse_off_len(p)     (((extra_info_t *) (p)->extra_info)->output_pulse_off_len)
#define set_output_pulse_off_len(p, v)  {((extra_info_t *) (p)->extra_info)->output_pulse_off_len = v;}

#define has_closed_input(p)             (get_closed_input(port) >= 0)
#define has_moving_input(p)             (get_moving_input(port) >= 0)

#define has_open_output(p)              (get_open_output(port) >= 0)
#define has_close_output(p)             (get_close_output(port) >= 0)
#define has_stop_output(p)              (get_stop_output(port) >= 0)

#define has_open_close(p)               (has_open_output(p) && has_close_output(p) && \
                                         get_open_output(p) != get_close_output(p))

#define pending_outputs(p)              (((extra_info_t *) (p)->extra_info)->pending_outputs)
#define pending_outputs_len(p)          (((extra_info_t *) (p)->extra_info)->pending_outputs_len)

#define current_output_state(p)         (((extra_info_t *) (p)->extra_info)->current_output_state)
#define current_output(p)               (((extra_info_t *) (p)->extra_info)->current_output)
#define current_output_rem(p)           (((extra_info_t *) (p)->extra_info)->current_output_rem)

#define last_heart_beat_time(p)         (((extra_info_t *) (p)->extra_info)->last_heart_beat_time)

#define closed_input_debounce_hist(p)   (((extra_info_t *) (p)->extra_info)->closed_input_debounce_hist)
#define moving_input_debounce_hist(p)   (((extra_info_t *) (p)->extra_info)->moving_input_debounce_hist)

#define closed_input_delay_hist(p)      (((extra_info_t *) (p)->extra_info)->closed_input_delay_hist)
#define moving_input_delay_hist(p)      (((extra_info_t *) (p)->extra_info)->moving_input_delay_hist)

static int8                             input_mapping[] = {-1, 0, 4, 15};
static char                           * input_choices[] = {"none", "input 0", "input 4", "input 15", NULL};

static int8                             output_mapping[] = {-1, 5, 14};
static char                           * output_choices[] = {"none", "output 5", "output 14", NULL};

static char                           * level_choices[] = {"low", "high", NULL};

#if defined(_DEBUG) && defined(_DEBUG_FSG)

static char *STATE_STR[] = {

    "CLOSING",
    "CLOSED",
    "STOPPED",
    "OPENED",
    "OPENING",
    "OPENED_WILL_OPEN",
    "OPENED_WILL_CLOSE"

};

static char *CMD_STR[] = {

    "CLOSE",
    "STOP",
    "OPEN"

};

#endif


ICACHE_FLASH_ATTR static double     read_value(port_t *port);
ICACHE_FLASH_ATTR static bool       write_value(port_t *port, double value);
ICACHE_FLASH_ATTR static void       configure(port_t *port);
ICACHE_FLASH_ATTR static void       heart_beat(port_t *port);

ICACHE_FLASH_ATTR static int8       is_closed(port_t *port);
ICACHE_FLASH_ATTR static int8       is_moving(port_t *port);

ICACHE_FLASH_ATTR static void       push_cmd(port_t *port, int8 cmd);
ICACHE_FLASH_ATTR static int8       pop_output(port_t *port);

ICACHE_FLASH_ATTR static int        attr_get_closed_input(port_t *port);
ICACHE_FLASH_ATTR static void       attr_set_closed_input(port_t *port, int index);

ICACHE_FLASH_ATTR static int        attr_get_moving_input(port_t *port);
ICACHE_FLASH_ATTR static void       attr_set_moving_input(port_t *port, int index);

ICACHE_FLASH_ATTR static int        attr_get_closed_input_level(port_t *port);
ICACHE_FLASH_ATTR static void       attr_set_closed_input_level(port_t *port, int index);

ICACHE_FLASH_ATTR static int        attr_get_moving_input_level(port_t *port);
ICACHE_FLASH_ATTR static void       attr_set_moving_input_level(port_t *port, int index);

ICACHE_FLASH_ATTR static int        attr_get_debounce_duration(port_t *port);
ICACHE_FLASH_ATTR static void       attr_set_debounce_duration(port_t *port, int value);

ICACHE_FLASH_ATTR static int        attr_get_closed_moving_delay(port_t *port);
ICACHE_FLASH_ATTR static void       attr_set_closed_moving_delay(port_t *port, int value);

ICACHE_FLASH_ATTR static int        attr_get_open_output(port_t *port);
ICACHE_FLASH_ATTR static void       attr_set_open_output(port_t *port, int index);

ICACHE_FLASH_ATTR static int        attr_get_close_output(port_t *port);
ICACHE_FLASH_ATTR static void       attr_set_close_output(port_t *port, int index);

ICACHE_FLASH_ATTR static int        attr_get_stop_output(port_t *port);
ICACHE_FLASH_ATTR static void       attr_set_stop_output(port_t *port, int index);

ICACHE_FLASH_ATTR static int        attr_get_output_level(port_t *port);
ICACHE_FLASH_ATTR static void       attr_set_output_level(port_t *port, int index);

ICACHE_FLASH_ATTR static int        attr_get_output_pulse_on_len(port_t *port);
ICACHE_FLASH_ATTR static void       attr_set_output_pulse_on_len(port_t *port, int value);

ICACHE_FLASH_ATTR static int        attr_get_output_pulse_off_len(port_t *port);
ICACHE_FLASH_ATTR static void       attr_set_output_pulse_off_len(port_t *port, int value);


static attrdef_t closed_input_attrdef = {

    .name = "closed_input",
    .display_name = "Closed GPIO",
    .description = "Closed state input pin.",
    .type = ATTR_TYPE_STRING,
    .modifiable = TRUE,
    .set = attr_set_closed_input,
    .get = attr_get_closed_input,
    .choices = input_choices

};

static attrdef_t moving_input_attrdef = {

    .name = "moving_input",
    .display_name = "Moving GPIO",
    .description = "Moving state input pin.",
    .type = ATTR_TYPE_STRING,
    .modifiable = TRUE,
    .set = attr_set_moving_input,
    .get = attr_get_moving_input,
    .choices = input_choices

};

static attrdef_t closed_input_level_attrdef = {

    .name = "closed_input_level",
    .display_name = "Closed Input Level",
    .description = "Closed input active level.",
    .type = ATTR_TYPE_STRING,
    .modifiable = TRUE,
    .set = attr_set_closed_input_level,
    .get = attr_get_closed_input_level,
    .choices = level_choices

};

static attrdef_t debounce_duration_attrdef = {

    .name = "debounce_duration",
    .display_name = "Debounce Duration",
    .description = "The time window used by the debouncing filter.",
    .unit = "milliseconds",
    .type = ATTR_TYPE_NUMBER,
    .modifiable = TRUE,
    .min = DEBOUNCE_DURATION_MIN,
    .max = DEBOUNCE_DURATION_MAX,
    .step = HEART_BEAT_INTERVAL,
    .integer = TRUE,
    .set = attr_set_debounce_duration,
    .get = attr_get_debounce_duration

};

static attrdef_t closed_moving_delay_attrdef = {

    .name = "closed_moving_delay",
    .display_name = "Closed/Moving Delay",
    .description = "Delay between closed and moving inputs (can be negative).",
    .unit = "milliseconds",
    .type = ATTR_TYPE_NUMBER,
    .modifiable = TRUE,
    .min = CLOSED_MOVING_DELAY_MIN,
    .max = CLOSED_MOVING_DELAY_MAX,
    .step = HEART_BEAT_INTERVAL,
    .integer = TRUE,
    .set = attr_set_closed_moving_delay,
    .get = attr_get_closed_moving_delay

};

static attrdef_t moving_input_level_attrdef = {

    .name = "moving_input_level",
    .display_name = "Moving Input Level",
    .description = "Moving input active level.",
    .type = ATTR_TYPE_STRING,
    .modifiable = TRUE,
    .set = attr_set_moving_input_level,
    .get = attr_get_moving_input_level,
    .choices = level_choices

};

static attrdef_t open_output_attrdef = {

    .name = "open_output",
    .display_name = "Open GPIO",
    .description = "Open command output pin.",
    .type = ATTR_TYPE_STRING,
    .modifiable = TRUE,
    .set = attr_set_open_output,
    .get = attr_get_open_output,
    .choices = output_choices

};

static attrdef_t close_output_attrdef = {

    .name = "close_output",
    .display_name = "Close GPIO",
    .description = "Close command output pin.",
    .type = ATTR_TYPE_STRING,
    .modifiable = TRUE,
    .set = attr_set_close_output,
    .get = attr_get_close_output,
    .choices = output_choices

};

static attrdef_t stop_output_attrdef = {

    .name = "stop_output",
    .display_name = "Stop GPIO",
    .description = "Stop command output pin.",
    .type = ATTR_TYPE_STRING,
    .modifiable = TRUE,
    .set = attr_set_stop_output,
    .get = attr_get_stop_output,
    .choices = output_choices

};

static attrdef_t output_level_attrdef = {

    .name = "output_level",
    .display_name = "Output Level",
    .description = "Command output active level.",
    .type = ATTR_TYPE_STRING,
    .modifiable = TRUE,
    .set = attr_set_output_level,
    .get = attr_get_output_level,
    .choices = level_choices

};

static attrdef_t output_pulse_on_len_attrdef = {

    .name = "output_pulse_on_len",
    .display_name = "Output Pulse ON Length",
    .description = "Command output pulse on length.",
    .unit = "milliseconds",
    .type = ATTR_TYPE_NUMBER,
    .modifiable = TRUE,
    .min = PULSE_LEN_MIN,
    .max = PULSE_LEN_MAX,
    .step = HEART_BEAT_INTERVAL,
    .integer = TRUE,
    .set = attr_set_output_pulse_on_len,
    .get = attr_get_output_pulse_on_len

};

static attrdef_t output_pulse_off_len_attrdef = {

    .name = "output_pulse_off_len",
    .display_name = "Output Pulse OFF Length",
    .description = "Command output pulse off length.",
    .unit = "milliseconds",
    .type = ATTR_TYPE_NUMBER,
    .modifiable = TRUE,
    .min = PULSE_LEN_MIN,
    .max = PULSE_LEN_MAX,
    .step = HEART_BEAT_INTERVAL,
    .integer = TRUE,
    .set = attr_set_output_pulse_off_len,
    .get = attr_get_output_pulse_off_len

};

static attrdef_t *attrdefs[] = {

    &closed_input_attrdef,
    &moving_input_attrdef,
    &closed_input_level_attrdef,
    &moving_input_level_attrdef,
    &debounce_duration_attrdef,
    &closed_moving_delay_attrdef,
    &open_output_attrdef,
    &close_output_attrdef,
    &stop_output_attrdef,
    &output_level_attrdef,
    &output_pulse_on_len_attrdef,
    &output_pulse_off_len_attrdef,
    NULL

};

#ifdef HAS_FSG0
static extra_info_t fsg0_extra_info = {

    .state = STATE_CLOSED,
    .last_input_state = 0,

    .pending_outputs = NULL,
    .pending_outputs_len = 0,

    .current_output_state = OUTPUT_STATE_IDLE,
    .current_output = -1,
    .current_output_rem = -1,

    .last_heart_beat_time = 0

};

static port_t _fsg0 = {

    .slot = PORT_SLOT_EXTRA0,

    .id = FSG0_ID,
    .type = PORT_TYPE_NUMBER,
    .min = STATE_CLOSING,
    .max = STATE_OPENING,
    .step = UNDEFINED,
    .integer = TRUE,

    .flags = PORT_FLAG_OUTPUT,
    .extra_info = &fsg0_extra_info,

    .heart_beat_interval = HEART_BEAT_INTERVAL,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure,
    .heart_beat = heart_beat,

    .attrdefs = attrdefs

};

port_t *fsg0 = &_fsg0;
#endif

#ifdef HAS_FSG1
static extra_info_t fsg1_extra_info = {

    .state = STATE_CLOSED,
    .last_input_state = 0,

    .pending_outputs = NULL,
    .pending_outputs_len = 0,

    .current_output_state = OUTPUT_STATE_IDLE,
    .current_output = -1,
    .current_output_rem = -1,

    .last_heart_beat_time = 0

};

static port_t _fsg1 = {

    .slot = PORT_SLOT_EXTRA1,

    .id = FSG1_ID,
    .type = PORT_TYPE_NUMBER,
    .min = STATE_CLOSING,
    .max = STATE_OPENING,
    .step = UNDEFINED,
    .integer = TRUE,

    .flags = PORT_FLAG_OUTPUT,
    .extra_info = &fsg1_extra_info,

    .heart_beat_interval = HEART_BEAT_INTERVAL,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure,
    .heart_beat = heart_beat,

    .attrdefs = attrdefs

};

port_t *fsg1 = &_fsg1;
#endif

#ifdef HAS_FSG2
static extra_info_t fsg2_extra_info = {

    .state = STATE_CLOSED,
    .last_input_state = 0,

    .pending_outputs = NULL,
    .pending_outputs_len = 0,

    .current_output_state = OUTPUT_STATE_IDLE,
    .current_output = -1,
    .current_output_rem = -1,

    .last_heart_beat_time = 0

};

static port_t _fsg2 = {

    .slot = PORT_SLOT_EXTRA2,

    .id = FSG2_ID,
    .type = PORT_TYPE_NUMBER,
    .min = STATE_CLOSING,
    .max = STATE_OPENING,
    .integer = TRUE,
    .step = UNDEFINED,

    .flags = PORT_FLAG_OUTPUT,
    .extra_info = &fsg2_extra_info,

    .heart_beat_interval = HEART_BEAT_INTERVAL,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure,
    .heart_beat = heart_beat,

    .attrdefs = attrdefs

};

port_t *fsg2 = &_fsg2;
#endif

#ifdef HAS_FSG3
static extra_info_t fsg3_extra_info = {

    .state = STATE_CLOSED,
    .last_input_state = 3,

    .pending_outputs = NULL,
    .pending_outputs_len = 3,

    .current_output_state = OUTPUT_STATE_IDLE,
    .current_output = -1,
    .current_output_rem = -1,

    .last_heart_beat_time = 3

};

static port_t _fsg3 = {

    .slot = PORT_SLOT_EXTRA3,

    .id = FSG3_ID,
    .type = PORT_TYPE_NUMBER,
    .min = STATE_CLOSING,
    .max = STATE_OPENING,
    .step = UNDEFINED,
    .integer = TRUE,

    .flags = PORT_FLAG_OUTPUT,
    .extra_info = &fsg3_extra_info,

    .heart_beat_interval = HEART_BEAT_INTERVAL,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure,
    .heart_beat = heart_beat,

    .attrdefs = attrdefs

};

port_t *fsg3 = &_fsg3;
#endif

#ifdef HAS_FSG4
static extra_info_t fsg4_extra_info = {

    .state = STATE_CLOSED,
    .last_input_state = 4,

    .pending_outputs = NULL,
    .pending_outputs_len = 4,

    .current_output_state = OUTPUT_STATE_IDLE,
    .current_output = -1,
    .current_output_rem = -1,

    .last_heart_beat_time = 4

};

static port_t _fsg4 = {

    .slot = PORT_SLOT_EXTRA4,

    .id = FSG4_ID,
    .type = PORT_TYPE_NUMBER,
    .min = STATE_CLOSING,
    .max = STATE_OPENING,
    .step = UNDEFINED,
    .integer = TRUE,

    .flags = PORT_FLAG_OUTPUT,
    .extra_info = &fsg4_extra_info,

    .heart_beat_interval = HEART_BEAT_INTERVAL,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure,
    .heart_beat = heart_beat,

    .attrdefs = attrdefs

};

port_t *fsg4 = &_fsg4;
#endif

#ifdef HAS_FSG5
static extra_info_t fsg5_extra_info = {

    .state = STATE_CLOSED,
    .last_input_state = 0,

    .pending_outputs = NULL,
    .pending_outputs_len = 0,

    .current_output_state = OUTPUT_STATE_IDLE,
    .current_output = -1,
    .current_output_rem = -1,

    .last_heart_beat_time = 0

};

static port_t _fsg5 = {

    .slot = PORT_SLOT_EXTRA5,

    .id = FSG5_ID,
    .type = PORT_TYPE_NUMBER,
    .min = STATE_CLOSING,
    .max = STATE_OPENING,
    .step = UNDEFINED,
    .integer = TRUE,

    .flags = PORT_FLAG_OUTPUT,
    .extra_info = &fsg5_extra_info,

    .heart_beat_interval = HEART_BEAT_INTERVAL,

    .read_value = read_value,
    .write_value = write_value,
    .configure = configure,
    .heart_beat = heart_beat,

    .attrdefs = attrdefs

};

port_t *fsg5 = &_fsg5;
#endif


double read_value(port_t *port) {
    if (pending_outputs_len(port) > 0 || current_output_state(port) != OUTPUT_STATE_IDLE) {
        /* refuse to return a value while we have more than one pending command,
         * or if we're currently running an output command;
         * this prevents unwanted port intermediate value switching when
         * the final state is reached after executing more than one command */

        return UNDEFINED;
    }

    int8 state = get_state(port);
    if (state == STATE_OPENED_WILL_CLOSE || state == STATE_OPENED_WILL_OPEN) {
        state = STATE_OPENED;
    }

    return state;
}

bool write_value(port_t *port, double value) {
    int8 current_state = get_state(port);


#if defined(_DEBUG) && defined(_DEBUG_FSG)
    DEBUG_FSG(port, "requesting state %s (current state %s)",
              STATE_STR[(int) value + 2], STATE_STR[(int) current_state + 2]);
#endif

    if (value == STATE_STOPPED) {
        /* force interval port value to new value,
         * so that a value-change event will be triggered,
         * knowing that read_value() will never return STATE_STOPPED */
        port->value = value;
    }

     switch ((int) value) {
         case STATE_CLOSED:  /* CLOSE requested */
         case STATE_CLOSING:

             switch (current_state) {
                 case STATE_OPENED:
                     push_cmd(port, CMD_CLOSE);
                     break;

                 case STATE_OPENED_WILL_OPEN:
                     push_cmd(port, CMD_OPEN);
                     push_cmd(port, CMD_STOP);
                     push_cmd(port, CMD_CLOSE);
                     break;

                 case STATE_OPENED_WILL_CLOSE:
                     push_cmd(port, CMD_CLOSE);
                     break;

                 case STATE_OPENING:
                     push_cmd(port, CMD_STOP);
                     push_cmd(port, CMD_CLOSE);
                     break;
             }

             break;

         case STATE_OPENED:  /* OPEN requested */
         case STATE_OPENING:

             switch (current_state) {
                 case STATE_CLOSED:
                     push_cmd(port, CMD_OPEN);
                     break;

                 case STATE_CLOSING:
                     push_cmd(port, CMD_STOP);
                     push_cmd(port, CMD_OPEN);
                     break;

                 case STATE_OPENED_WILL_OPEN:
                     push_cmd(port, CMD_OPEN);
                     break;

                 case STATE_OPENED_WILL_CLOSE:
                     push_cmd(port, CMD_CLOSE);
                     push_cmd(port, CMD_STOP);
                     push_cmd(port, CMD_OPEN);
                     break;
             }

             break;

         case STATE_STOPPED:  /* STOP requested */

             switch (current_state) {
                 case STATE_OPENING:
                 case STATE_CLOSING:
                     push_cmd(port, CMD_STOP);
                     break;
             }

             break;

         default:
             return FALSE;
     }

     return TRUE;
}

void configure(port_t *port) {
    /* set the GPIO function to all used pins */
    gpio_select_func(0);
    gpio_select_func(4);
    gpio_select_func(5);
    gpio_select_func(14);
    gpio_select_func(15);

    /* configure pull-ups */
    gpio_set_pullup(0, false);
    gpio_set_pullup(4, false);
    gpio_set_pullup(5, false);
    gpio_set_pullup(14, false);
    gpio_set_pullup(15, false);

    /* set input GPIOs */
    GPIO_DIS_OUTPUT(0);
    GPIO_DIS_OUTPUT(4);
    GPIO_DIS_OUTPUT(15);

    DEBUG_FSG(port, "closed input         = %d", get_closed_input(port));
    DEBUG_FSG(port, "closed input level   = %d", get_closed_input_level(port));
    DEBUG_FSG(port, "moving input         = %d", get_moving_input(port));
    DEBUG_FSG(port, "moving input level   = %d", get_moving_input_level(port));
    DEBUG_FSG(port, "debounce duration    = %d ms", get_debounce_duration(port));
    DEBUG_FSG(port, "closed/moving delay  = %d ms", get_closed_moving_delay(port));
    DEBUG_FSG(port, "open output          = %d", get_open_output(port));
    DEBUG_FSG(port, "close output         = %d", get_close_output(port));
    DEBUG_FSG(port, "stop output          = %d", get_stop_output(port));
    DEBUG_FSG(port, "output level         = %d", get_output_level(port));
    DEBUG_FSG(port, "output pulse on len  = %d ms", get_output_pulse_on_len(port));
    DEBUG_FSG(port, "output pulse off len = %d ms", get_output_pulse_off_len(port));

    /* set the last input state and history,
     * considering initial state "closed" and "not moving" */
    uint8 last_input_state = CLOSED_INPUT_STATE_MASK;
    uint8 closed_input_active_value = 1;
    uint8 moving_input_active_value = 1;
    if (!get_closed_input_level(port)) {
        closed_input_active_value = 0;
    }
    if (!get_moving_input_level(port)) {
        moving_input_active_value = 0;
    }

    memset(closed_input_debounce_hist(port), closed_input_active_value, INPUT_DEBOUNCE_HIST_LEN);
    memset(moving_input_debounce_hist(port), !moving_input_active_value, INPUT_DEBOUNCE_HIST_LEN);
    memset(closed_input_delay_hist(port), closed_input_active_value, INPUT_DELAY_HIST_LEN);
    memset(moving_input_delay_hist(port), !moving_input_active_value, INPUT_DELAY_HIST_LEN);

    set_last_input_state(port, last_input_state);
    set_state(port, STATE_CLOSED);

    /* set outputs to corresponding inactive value */
    int8 output;
    bool output_value = !get_output_level(port);
    output = get_close_output(port);
    if (output) {
        GPIO_OUTPUT_SET(output, output_value);
        DEBUG_FSG(port, "setting close output %d to OFF (%d)", output, output_value);
    }
    output = get_open_output(port);
    if (output) {
        GPIO_OUTPUT_SET(output, output_value);
        DEBUG_FSG(port, "setting open output %d to OFF (%d)", output, output_value);
    }
    output = get_stop_output(port);
    if (output) {
        GPIO_OUTPUT_SET(output, output_value);
        DEBUG_FSG(port, "setting output %d to OFF (%d)", output, output_value);
    }
}

void heart_beat(port_t *port) {
    /* delay the actual processing by a few seconds, at boot,
     * allowing the gate equipment to properly start up */
    if (system_uptime_us() / 1000 < BOOT_DELAY) {
        return;
    }

    /* poll all input ports */
    int8 closed = is_closed(port);
    int8 moving = is_moving(port);

    bool has_so = has_stop_output(port);
    bool has_oc = has_open_close(port);

    int8 current_state = get_state(port);
    int8 new_state = current_state;
    int8 last_input_state = get_last_input_state(port);

    bool moving_changed = (moving != -1) && (moving != !!(last_input_state & MOVING_INPUT_STATE_MASK));

    /* moving input has lower priority when deciding the new state */
    if (moving_changed) {
        DEBUG_FSG(port, "moving state: %d -> %d", !moving, moving);

        switch (current_state) {
            case STATE_CLOSED:
                if (moving) {
                    new_state = STATE_OPENING;
                }
                break;

            case STATE_OPENED:
                if (moving) {
                    if (!has_so) {
                        new_state = STATE_CLOSING;
                    }
                    else {
                        /* there's no way to tell from a single moving input whether the door is opening or closing */
                    }
                }
                break;

            case STATE_OPENED_WILL_CLOSE:
                if (moving) {
                    new_state = STATE_CLOSING;
                }
                break;

            case STATE_OPENED_WILL_OPEN:
                if (moving) {
                    new_state = STATE_OPENING;
                }
                break;

            case STATE_OPENING:
                if (!moving) {
                    if (has_oc) {
                        new_state = STATE_OPENED;
                    }
                    else {
                        new_state = STATE_OPENED_WILL_CLOSE;
                    }
                }
                break;

            case STATE_CLOSING:
                if (!moving) {
                    if (!has_so) {
                        new_state = STATE_CLOSED;
                    }
                    else {
                        new_state = STATE_OPENED_WILL_OPEN;
                    }
                }
                break;
        }
    }

    /* closed input has the highest priority */
    if (closed && current_state != STATE_CLOSED) {
        DEBUG_FSG(port,"closed state: %d -> %d", !closed, closed);

        new_state = STATE_CLOSED;
    }

    /* change state */
    if (current_state != new_state) {
#if defined(_DEBUG) && defined(_DEBUG_FSG)
        DEBUG_FSG(port, "state changed: %s -> %s", STATE_STR[current_state + 2], STATE_STR[new_state + 2]);
#endif

        set_state(port, new_state);
    }

    /* update last input state */
    last_input_state = 0;
    if (closed != -1) {
        last_input_state |= closed;
    }
    if (moving != -1) {
        last_input_state |= moving << 1;
    }

    set_last_input_state(port, last_input_state);

    /* process output */
    uint64 now = system_uptime_us();
    uint32 delta = now - last_heart_beat_time(port);

    switch (current_output_state(port)) {
        case OUTPUT_STATE_IDLE: {
            /* pop next output from queue */
            uint32 output = pop_output(port);
            if (output != -1) {
                GPIO_OUTPUT_SET(output, get_output_level(port));
                DEBUG_FSG(port, "setting output %d to ON (%d)", output, get_output_level(port));

                current_output_rem(port) = get_output_pulse_on_len(port) * 1000;  /* we deal in microseconds here */
                current_output(port) = output;
                current_output_state(port) = OUTPUT_STATE_ON;
            }

            break;
        }

        case OUTPUT_STATE_ON: {
            current_output_rem(port) -= delta;
            if (current_output_rem(port) <= 0) {
                GPIO_OUTPUT_SET(current_output(port), !get_output_level(port));
                DEBUG_FSG(port, "setting output %d to OFF (%d)", current_output(port), !get_output_level(port));

                current_output_rem(port) = get_output_pulse_off_len(port) * 1000;  /* we deal in microseconds here */
                current_output(port) = -1;
                current_output_state(port) = OUTPUT_STATE_OFF;
            }

            break;
        }

        case OUTPUT_STATE_OFF: {
            current_output_rem(port) -= delta;
            if (current_output_rem(port) <= 0) {
                DEBUG_FSG(port, "output state is now IDLE");

                current_output_state(port) = OUTPUT_STATE_IDLE;
            }

            break;
        }
    }

    last_heart_beat_time(port) = now;
}


int8 is_closed(port_t *port) {
    int8 input = get_closed_input(port);
    if (input < 0) {
        return -1;  /* input disabled */
    }

    bool value;
    int i, sum = 0;

    /* apply debouncing filter */
    int duration_count = get_debounce_duration(port) / HEART_BEAT_INTERVAL;
    if (duration_count > 0) {
        for (i = 0; i < duration_count - 1; i++) {
            closed_input_debounce_hist(port)[i] = closed_input_debounce_hist(port)[i + 1];
            sum += closed_input_debounce_hist(port)[i];
        }
        closed_input_debounce_hist(port)[duration_count - 1] = !!GPIO_INPUT_GET(input);
        sum += closed_input_debounce_hist(port)[duration_count - 1];

        /* the final filtered value is given by the majority of the history values */
        value = sum * 10 / duration_count > 5;
    }
    else {
        value = !!GPIO_INPUT_GET(input);
    }

    /* invert value, if necessary */
    if (!get_closed_input_level(port)) {
        value = !value;
    }

    /* apply delay */
    int32 delay = get_closed_moving_delay(port);
    if (delay < 0 && delay >= CLOSED_MOVING_DELAY_MIN) {  /* negative delay means delaying "closed" input */
        delay = -delay;
        int delay_count = delay / HEART_BEAT_INTERVAL;
        for (i = 0; i < delay_count - 1; i++) {
            closed_input_delay_hist(port)[i] = closed_input_delay_hist(port)[i + 1];
        }

        closed_input_delay_hist(port)[delay_count - 1] = value;
        value = closed_input_delay_hist(port)[0];
    }

    return value;
}

int8 is_moving(port_t *port) {
    int8 input = get_moving_input(port);
    if (input < 0) {
        // TODO add support for fixed moving time (e.g. 10s opening, 12s closing)
        // only makes sense for open-close cycles without stop

        return -1;  /* input disabled */
    }

    bool value;
    int i, sum = 0;

    /* apply debouncing filter */
    int duration_count = get_debounce_duration(port) / HEART_BEAT_INTERVAL;
    if (duration_count > 0) {
        for (i = 0; i < duration_count - 1; i++) {
            moving_input_debounce_hist(port)[i] = moving_input_debounce_hist(port)[i + 1];
            sum += moving_input_debounce_hist(port)[i];
        }
        moving_input_debounce_hist(port)[duration_count - 1] = !!GPIO_INPUT_GET(input);
        sum += moving_input_debounce_hist(port)[duration_count - 1];

        /* the final filtered value is given by the majority of the history values */
        value = sum * 10 / duration_count > 5;
    }
    else {
        value = !!GPIO_INPUT_GET(input);
    }

    /* invert value, if necessary */
    if (!get_moving_input_level(port)) {
        value = !value;
    }

    /* apply delay */
    int32 delay = get_closed_moving_delay(port);
    if (delay > 0 && delay <= CLOSED_MOVING_DELAY_MAX) {  /* positive delay means delaying "moving" input */
        int delay_count = delay / HEART_BEAT_INTERVAL;
        for (i = 0; i < delay_count - 1; i++) {
            moving_input_delay_hist(port)[i] = moving_input_delay_hist(port)[i + 1];
        }

        moving_input_delay_hist(port)[delay_count - 1] = value;
        value = moving_input_delay_hist(port)[0];
    }

    return value;
}

void push_cmd(port_t *port, int8 cmd) {
    int8 output = -1;
    int i;

    switch (cmd) {
        case CMD_CLOSE:
            output = get_close_output(port);
            break;

        case CMD_STOP:
            output = get_stop_output(port);
            break;

        case CMD_OPEN:
            output = get_open_output(port);
            break;
    }

    if (output == -1) {
#if defined(_DEBUG) && defined(_DEBUG_FSG)
        DEBUG_FSG(port, "command %s failed: output not defined", CMD_STR[cmd + 1]);
#endif
        return;
    }

#if defined(_DEBUG) && defined(_DEBUG_FSG)
    DEBUG_FSG(port, "pushing command %s", CMD_STR[cmd + 1]);
#endif

    /* make sure we have enough room for the new command */
    while (pending_outputs_len(port) >= PENDING_OUTPUTS_MAX_LEN) {
        for (i = 0; i < pending_outputs_len(port) - 1; i++) {
            pending_outputs(port)[i] = pending_outputs(port)[i + 1];
        }
        pending_outputs_len(port)--;
    }

    /* append the new command to the list */
    pending_outputs_len(port)++;
    pending_outputs(port) = realloc(pending_outputs(port), sizeof(uint8) * pending_outputs_len(port));
    pending_outputs(port)[pending_outputs_len(port) - 1] = (uint8) output;

    DEBUG_FSG(port, "%d pending commands after push", pending_outputs_len(port));
}

int8 pop_output(port_t *port) {
    if (!pending_outputs_len(port)) {
        return -1;
    }

    int i;
    uint8 output = pending_outputs(port)[0];
    for (i = 0; i < pending_outputs_len(port) - 1; i++) {
        pending_outputs(port)[i] = pending_outputs(port)[i + 1];
    }

    pending_outputs_len(port)--;

    pending_outputs(port) = realloc(pending_outputs(port), sizeof(uint8) * pending_outputs_len(port));

    DEBUG_FSG(port, "popped output %d (%d remaining pending commands)", output, pending_outputs_len(port));

    return output;
}

int attr_get_closed_input(port_t *port) {
    int i;
    int8 value;

    /* read from persisted data */
    memcpy(&value, port->extra_data + CLOSED_INPUT_CONFIG_OFFS, 1);

    /* update cached value */
    set_closed_input(port, value);

    for (i = 0; i < INPUT_CHOICES_LEN; i++) {
        if (input_mapping[i] == value) {
            /* return choice index */
            return i;
        }
    }

    return 0;
}

void attr_set_closed_input(port_t *port, int index) {
    uint8 value = input_mapping[index];

    /* update cached value */
    set_closed_input(port, value);

    /* write to persisted data */
    memcpy(port->extra_data + CLOSED_INPUT_CONFIG_OFFS, &value, 1);
}

int attr_get_moving_input(port_t *port) {
    int i;
    int8 value;

    /* read from persisted data */
    memcpy(&value, port->extra_data + MOVING_INPUT_CONFIG_OFFS, 1);

    /* update cached value */
    set_moving_input(port, value);

    for (i = 0; i < INPUT_CHOICES_LEN; i++) {
        if (input_mapping[i] == value) {
            /* return choice index */
            return i;
        }
    }

    return 0;
}

void attr_set_moving_input(port_t *port, int index) {
    uint8 value = input_mapping[index];

    /* update cached value */
    set_moving_input(port, value);

    /* write to persisted data */
    memcpy(port->extra_data + MOVING_INPUT_CONFIG_OFFS, &value, 1);
}

int attr_get_closed_input_level(port_t *port) {
    /* return choice index */
    return (int) get_closed_input_level(port);
}

void attr_set_closed_input_level(port_t *port, int index) {
    set_closed_input_level(port, !!index);
}

int attr_get_moving_input_level(port_t *port) {
    /* return choice index */
    return (int) get_moving_input_level(port);
}

void attr_set_moving_input_level(port_t *port, int index) {
    set_moving_input_level(port, !!index);
}

int attr_get_debounce_duration(port_t *port) {
    int16 value;
    uint32 duration;

    /* read from persisted data */
    memcpy(&value, port->extra_data + DEBOUNCE_DURATION_CONFIG_OFFS, 2);

    duration = value * HEART_BEAT_INTERVAL;

    /* update cached value */
    set_debounce_duration(port, duration);

    return duration;
}

void attr_set_debounce_duration(port_t *port, int value) {
    /* update cached value */
    set_debounce_duration(port, value);

    int16 config_value = value / HEART_BEAT_INTERVAL;

    /* write to persisted data */
    memcpy(port->extra_data + DEBOUNCE_DURATION_CONFIG_OFFS, &config_value, 2);
}

int attr_get_closed_moving_delay(port_t *port) {
    int16 value;
    uint32 delay;

    /* read from persisted data */
    memcpy(&value, port->extra_data + CLOSED_MOVING_DELAY_CONFIG_OFFS, 2);

    delay = value * HEART_BEAT_INTERVAL;

    /* update cached value */
    set_closed_moving_delay(port, delay);

    return delay;
}

void attr_set_closed_moving_delay(port_t *port, int value) {
    /* update cached value */
    set_closed_moving_delay(port, value);

    int16 config_value = value / HEART_BEAT_INTERVAL;

    /* write to persisted data */
    memcpy(port->extra_data + CLOSED_MOVING_DELAY_CONFIG_OFFS, &config_value, 2);
}

int attr_get_open_output(port_t *port) {
    int i;
    int8 value;

    /* read from persisted data */
    memcpy(&value, port->extra_data + OPEN_OUTPUT_CONFIG_OFFS, 1);

    /* update cached value */
    set_open_output(port, value);

    for (i = 0; i < OUTPUT_CHOICES_LEN; i++) {
        if (output_mapping[i] == value) {
            /* return choice index */
            return i;
        }
    }

    return 0;
}

void attr_set_open_output(port_t *port, int index) {
    uint8 value = output_mapping[index];

    /* update cached value */
    set_open_output(port, value);

    /* write to persisted data */
    memcpy(port->extra_data + OPEN_OUTPUT_CONFIG_OFFS, &value, 1);
}

int attr_get_close_output(port_t *port) {
    int i;
    int8 value;

    /* read from persisted data */
    memcpy(&value, port->extra_data + CLOSE_OUTPUT_CONFIG_OFFS, 1);

    /* update cached value */
    set_close_output(port, value);

    for (i = 0; i < OUTPUT_CHOICES_LEN; i++) {
        if (output_mapping[i] == value) {
            /* return choice index */
            return i;
        }
    }

    return 0;
}

void attr_set_close_output(port_t *port, int index) {
    uint8 value = output_mapping[index];

    /* update cached value */
    set_close_output(port, value);

    /* write to persisted data */
    memcpy(port->extra_data + CLOSE_OUTPUT_CONFIG_OFFS, &value, 1);
}

int attr_get_stop_output(port_t *port) {
    int i;
    int8 value;

    /* read from persisted data */
    memcpy(&value, port->extra_data + STOP_OUTPUT_CONFIG_OFFS, 1);

    /* update cached value */
    set_stop_output(port, value);

    for (i = 0; i < OUTPUT_CHOICES_LEN; i++) {
        if (output_mapping[i] == value) {
            /* return choice index */
            return i;
        }
    }

    return 0;
}

void attr_set_stop_output(port_t *port, int index) {
    uint8 value = output_mapping[index];

    /* update cached value */
    set_stop_output(port, value);

    /* write to persisted data */
    memcpy(port->extra_data + STOP_OUTPUT_CONFIG_OFFS, &value, 1);
}

int attr_get_output_level(port_t *port) {
    /* return choice index */
    return (int) get_output_level(port);
}

void attr_set_output_level(port_t *port, int index) {
    set_output_level(port, !!index);
}

int attr_get_output_pulse_on_len(port_t *port) {
    uint16 value;
    uint32 len;

    /* read from persisted data */
    memcpy(&value, port->extra_data + PULSE_ON_LEN_CONFIG_OFFS, 2);

    len = value * HEART_BEAT_INTERVAL;
    if (!len) {
        len = PULSE_LEN_DEF;
    }

    /* update cached value */
    set_output_pulse_on_len(port, len);

    return len;
}

void attr_set_output_pulse_on_len(port_t *port, int value) {
    /* update cached value */
    set_output_pulse_on_len(port, value);

    uint16 config_value = value / HEART_BEAT_INTERVAL;

    /* write to persisted data */
    memcpy(port->extra_data + PULSE_ON_LEN_CONFIG_OFFS, &config_value, 2);
}

int attr_get_output_pulse_off_len(port_t *port) {
    uint16 value;
    uint32 len;

    /* read from persisted data */
    memcpy(&value, port->extra_data + PULSE_OFF_LEN_CONFIG_OFFS, 2);

    len = value * HEART_BEAT_INTERVAL;
    if (!len) {
        len = PULSE_LEN_DEF;
    }

    /* update cached value */
    set_output_pulse_off_len(port, len);

    return len;
}

void attr_set_output_pulse_off_len(port_t *port, int value) {
    /* update cached value */
    set_output_pulse_off_len(port, value);

    uint16 config_value = value / HEART_BEAT_INTERVAL;

    /* write to persisted data */
    memcpy(port->extra_data + PULSE_OFF_LEN_CONFIG_OFFS, &config_value, 2);
}


void fsg_init_ports(void) {
#ifdef HAS_FSG0
    port_register(fsg0);
#endif

#ifdef HAS_FSG1
    port_register(fsg1);
#endif

#ifdef HAS_FSG2
    port_register(fsg2);
#endif

#ifdef HAS_FSG3
    port_register(fsg3);
#endif

#ifdef HAS_FSG4
    port_register(fsg4);
#endif

#ifdef HAS_FSG5
    port_register(fsg5);
#endif
}


#endif  /* HAS_FSG */
