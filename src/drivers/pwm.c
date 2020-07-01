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
 * Inspired from https://github.com/StefanBruens/ESP8266_new_pwm
 */


#include <c_types.h>
#include <eagle_soc.h>
#include <ets_sys.h>

#include "espgoodies/common.h"
#include "espgoodies/gpio.h"

#include "common.h"
#include "pwm.h"


#define USE_NMI FALSE

#define MAX_PERIOD 0x7FFFFF

#define TIMER1_DIVIDE_BY_16 0x0004
#define TIMER1_ENABLE_TIMER 0x0080


typedef struct {

	uint32 ticks;    /* Delay until next phase, in 200ns units */
	uint16 on_mask;  /* GPIO mask to switch on */
	uint16 off_mask; /* GPIO mask to switch off */

} pwm_phase_t;

typedef struct {

    uint32 out;         /* 0x60000300 */
    uint32 out_w1ts;    /* 0x60000304 */
    uint32 out_w1tc;    /* 0x60000308 */
    uint32 enable;      /* 0x6000030C */
    uint32 enable_w1ts; /* 0x60000310 */
    uint32 enable_w1tc; /* 0x60000314 */
    uint32 in;          /* 0x60000318 */
    uint32 status;      /* 0x6000031C */
    uint32 status_w1ts; /* 0x60000320 */
    uint32 status_w1tc; /* 0x60000324 */

} gpio_regs_t;

typedef struct {

    uint32 frc1_load;  /* 0x60000600 */
    uint32 frc1_count; /* 0x60000604 */
    uint32 frc1_ctrl;  /* 0x60000608 */
    uint32 frc1_int;   /* 0x6000060C */
    uint8  pad[16];
    uint32 frc2_load;  /* 0x60000620 */
    uint32 frc2_count; /* 0x60000624 */
    uint32 frc2_ctrl;  /* 0x60000628 */
    uint32 frc2_int;   /* 0x6000062C */
    uint32 frc2_alarm; /* 0x60000630 */

} timer_regs_t;


/* Three sets of PWM phases, the active one, the one used starting with the next cycle, and the one updated. After the
 * update next_phase is set to the last updated set. current_phase is set to next_phase from the interrupt routine
 * during the first PWM phase */
static pwm_phase_t  *phase_sets[3];

static uint8         current_phase_no;
static pwm_phase_t  *current_phase;
static pwm_phase_t  *next_phase;

static uint32        current_period;
static uint32       *current_duties;
static uint32       *gpio_masks;
static uint8        *gpio_channel_mapping;
static uint8         channel_count;

static gpio_regs_t  *gpio_regs = (gpio_regs_t *) 0x60000300;
static timer_regs_t *timer_regs = (timer_regs_t *) 0x60000600;

static bool          intr_handler_attached = FALSE;
static bool          enabled = FALSE;


ICACHE_FLASH_ATTR static void                             apply_config(void);
static void                                               intr_handler(void);
ICACHE_FLASH_ATTR static uint8 __attribute__ ((noinline)) prepare_phases(pwm_phase_t *phases);


void pwm_setup(uint32 gpio_mask) {
    int i;
    uint32 mask = gpio_mask;

    /* Count number of channels used */
    channel_count = 0;
    uint8 gpio_no = 0;
    while (mask) {
        if (mask % 2) {
            channel_count++;
        }
        mask >>= 1;
        gpio_no++;
    }

    /* Free any previously allocated memory for PWM channels */
    for (i = 0; i < 3; i++) {
        free(phase_sets[i]);
        phase_sets[i] = NULL;
    }

    free(current_duties);
    free(gpio_masks);
    free(gpio_channel_mapping);

    current_duties = NULL;
    gpio_masks = NULL;
    gpio_channel_mapping = NULL;

    if (!channel_count) {
        return;
    }

    DEBUG_PWM("setting up with GPIO mask %08X (%d channels)", gpio_mask, channel_count);

    /* Allocate memory for channels */
    for (i = 0; i < 3; i++) {
        phase_sets[i] = zalloc(sizeof(pwm_phase_t) * (channel_count + 2));
    }

    current_duties = zalloc(sizeof(uint32) * channel_count);
    gpio_masks = zalloc(sizeof(uint32) * channel_count);

    /* Allocate mapping array according to gpio_no which is the highest GPIO used */
    gpio_channel_mapping = zalloc(sizeof(uint8) * (gpio_no + 1));

    /* Setup GPIOs */
    gpio_no = 0;
    mask = gpio_mask;
    i = 0;
    while (mask) {
        if (mask % 2) {
            gpio_configure_output(gpio_no, /* initial = */ FALSE);
            gpio_masks[i] = 1UL << gpio_no;
            gpio_channel_mapping[gpio_no] = i;
            i++;
        }
        gpio_no++;
        mask >>= 1;
    }

    current_phase_no = 0;
    current_phase = NULL;
    next_phase = NULL;

    current_period = 5000; /* 5e6 / 1000 Hz */

    /* Attach interrupt handler, but only once, upon first pwm_setup() call */
    if (!intr_handler_attached) {
        intr_handler_attached = TRUE;
#if USE_NMI
        ETS_FRC_TIMER1_NMI_INTR_ATTACH((ets_isr_t) intr_handler);
#else
        ETS_FRC_TIMER1_INTR_ATTACH((ets_isr_t) intr_handler, NULL);
#endif
    }

    TM1_EDGE_INT_ENABLE();

    timer_regs->frc1_int &= ~FRC1_INT_CLR_MASK;
    timer_regs->frc1_ctrl = 0;

    apply_config();
}

void pwm_enable(void) {
    if (enabled) {
        return;
    }

    DEBUG_PWM("enabling");

    enabled = TRUE;
}

void pwm_disable(void) {
    if (!enabled) {
        return;
    }

    DEBUG_PWM("disabling");

    enabled = FALSE;
}

void pwm_set_duty(uint8 gpio_no, uint8 duty) {
    uint8 channel = gpio_channel_mapping[gpio_no];
    if (channel > channel_count || duty > 100) {
        return;
    }

    current_duties[channel] = duty * current_period / 100;

    DEBUG_PWM("setting duty for GPIO%d to %d%% (%d ms)", gpio_no, duty, current_duties[channel]);

    apply_config();
}

uint8 pwm_get_duty(uint8 gpio_no) {
    uint8 channel = gpio_channel_mapping[gpio_no];
    if (channel > channel_count) {
        return 0;
    }

    return current_duties[channel] * 100 / current_period;
}

void pwm_set_freq(uint32 freq) {
    uint32 new_period;
    if (freq == 0) {
        new_period = 0;
    }
    else {
        new_period = 5000000 / freq;
    }
    if (new_period > MAX_PERIOD) {
        new_period = MAX_PERIOD;
    }

    DEBUG_PWM("setting freq to %d Hz (%d ms)", freq, new_period);

    /* Recalculate duties as they are relative to current period */
    uint8 duty_percent;
    for (int i = 0; i < channel_count; i++) {
        duty_percent = current_period ? current_duties[i] * 100 / current_period : 0;
        current_duties[i] = duty_percent * new_period / 100;
        DEBUG_PWM("updating duty for channel %d to %d%% (%d ms)", i, duty_percent, current_duties[i]);
    }

    current_period = new_period;

    apply_config();
}

uint32 pwm_get_freq(void) {
    if (current_period == 0) {
        return 0;
    }

    return 5000000 / current_period;
}


void apply_config(void) {
    pwm_phase_t **phase = phase_sets;

    DEBUG_PWM("applying config");

    if ((*phase == next_phase) || (*phase == current_phase)) {
        phase++;
    }
    if ((*phase == next_phase) || (*phase == current_phase)) {
        phase++;
    }

    uint8 phase_count = prepare_phases(*phase);

    /* All with 0% / 100% duty - stop timer */
    if (phase_count == 1) {
        if (next_phase) {
            timer_regs->frc1_ctrl = 0;
            ETS_FRC1_INTR_DISABLE();
        }
        next_phase = NULL;

        GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, (*phase)[0].on_mask);
        GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, (*phase)[0].off_mask);

        return;
    }

    /* Start if not already started */
    if (!next_phase) {
        current_phase = next_phase = *phase;
        current_phase_no = phase_count - 1;
        ETS_FRC1_INTR_ENABLE();
        RTC_REG_WRITE(FRC1_LOAD_ADDRESS, 0);
        timer_regs->frc1_ctrl = TIMER1_DIVIDE_BY_16 | TIMER1_ENABLE_TIMER;
        return;
    }

    next_phase = *phase;
}

void intr_handler(void) {
    if (!channel_count || !current_period || !enabled) {
        return;
    }

	if ((current_phase[current_phase_no].off_mask == 0) &&
	    (current_phase[current_phase_no].on_mask == 0)) {

		current_phase = next_phase;
		current_phase_no = 0;
	}

	do {
		/* Force write to GPIO registers on each loop */
		__asm__ __volatile__ ("" : : : "memory");

		gpio_regs->out_w1ts = (uint32) (current_phase[current_phase_no].on_mask);
		gpio_regs->out_w1tc = (uint32) (current_phase[current_phase_no].off_mask);

		uint32 ticks = current_phase[current_phase_no].ticks;

		current_phase_no++;

		if (ticks) {
			if (ticks >= 16) {
				/* Constant interrupt overhead */
				ticks -= 9;
				timer_regs->frc1_int &= ~FRC1_INT_CLR_MASK;
				WRITE_PERI_REG(&timer_regs->frc1_load, ticks);
				return;
			}

			ticks *= 4;
			do {
				ticks -= 1;
				/* Stop compiler from optimizing delay loop to noop */
				__asm__ __volatile__ ("" : : : "memory");
			} while (ticks > 0);
		}

	} while (1);
}

uint8 prepare_phases(pwm_phase_t *phases) {
	uint8 i, phase_count;

	for (i = 0; i < channel_count + 2; i++) {
	    memset(phases + i, 0, sizeof(pwm_phase_t));
	}
	phase_count = 1;
	for (i = 0; i < channel_count; i++) {
		uint32 ticks = current_duties[i];
		if (ticks == 0) {
			phases[0].off_mask |= gpio_masks[i];
		}
		else if (ticks >= current_period) {
			phases[0].on_mask |= gpio_masks[i];
		}
		else {
			if (ticks < (current_period / 2)) {
				phases[phase_count].ticks = ticks;
				phases[0].on_mask |= gpio_masks[i];
				phases[phase_count].off_mask = gpio_masks[i];
			}
			else {
				phases[phase_count].ticks = current_period - ticks;
				phases[phase_count].on_mask = gpio_masks[i];
				phases[0].off_mask |= gpio_masks[i];
			}
			phase_count++;
		}
	}
	phases[phase_count].ticks = current_period;

	/* Bubble sort, lowest to highest duty */
	i = 2;
	while (i < phase_count) {
		if (phases[i].ticks < phases[i - 1].ticks) {
			pwm_phase_t p = phases[i];
			phases[i] = phases[i - 1];
			phases[i - 1] = p;
			if (i > 2) {
				i--;
			}
		}
		else {
			i++;
		}
	}

	/* Shift left to align right edge */
	uint8 l = 0, r = 1;
	while (r <= phase_count) {
		uint32 diff = phases[r].ticks - phases[l].ticks;
		if (diff && (diff <= 16)) {
			uint16 mask = phases[r].on_mask | phases[r].off_mask;

			phases[l].off_mask ^= phases[r].off_mask;
			phases[l].on_mask ^= phases[r].on_mask;
			phases[0].off_mask ^= phases[r].on_mask;
			phases[0].on_mask ^= phases[r].off_mask;
			phases[r].ticks = current_period - diff;
			phases[r].on_mask ^= mask;
			phases[r].off_mask ^= mask;
		}
		else {
			l = r;
		}
		r++;
	}

	/* Sort again */
	i = 2;
	while (i <= phase_count) {
		if (phases[i].ticks < phases[i - 1].ticks) {
			pwm_phase_t p = phases[i];
			phases[i] = phases[i - 1];
			phases[i - 1] = p;
			if (i > 2) {
				i--;
			}
		}
		else {
			i++;
		}
	}

	/* Merge same duty */
	l = 0, r = 1;
	while (r <= phase_count) {
		if (phases[r].ticks == phases[l].ticks) {
			phases[l].off_mask |= phases[r].off_mask;
			phases[l].on_mask |= phases[r].on_mask;
			phases[r].on_mask = 0;
			phases[r].off_mask = 0;
		}
		else {
			l++;
			if (l != r) {
				pwm_phase_t p = phases[l];
				phases[l] = phases[r];
				phases[r] = p;
			}
		}
		r++;
	}
	phase_count = l;

	/* Transform absolute end time to phase durations */
	for (i = 0; i < phase_count; i++) {
		phases[i].ticks = phases[i + 1].ticks - phases[i].ticks;
		/* Subtract common overhead */
		phases[i].ticks--;
	}
	phases[phase_count].ticks = 0;

	/* Do a cyclic shift if last phase is short */
	if (phases[phase_count - 1].ticks < 16) {
		for (i = 0; i < phase_count - 1; i++) {
			pwm_phase_t p = phases[i];
			phases[i] = phases[i + 1];
			phases[i + 1] = p;
		}
	}

	return phase_count;
}
