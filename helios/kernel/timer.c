/**
 * @file kernel/timer.c
 *
 * Copyright (C) 2025  Dylan Parks
 *
 * This file is part of HeliOS
 *
 * HeliOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "kernel/spinlock.h"
#include "lib/list.h"
#include "mm/kmalloc.h"
#include <arch/idt.h>
#include <arch/ports.h>
#include <kernel/tasks/scheduler.h>
#include <kernel/timer.h>
#include <kernel/types.h>
#include <lib/log.h>

static struct timer_subsystem ts = {
	.active_timers = LIST_HEAD_INIT(ts.active_timers),
	.lock = SPINLOCK_INIT,
	.current_ticks = 0,
	.seconds_since_start = 0,
	.tick_frequency = TIMER_HERTZ,
};

// Some IBM employee had a very fun time when designing this fucker.
static constexpr u32 PIT_CLK = 1193180;

/* This will keep track of how many ticks that the system
 *  has been running for */
// volatile uint64_t ticks = 0;
// volatile uint64_t seconds_since_start = 0;
static uint32_t phase = 18;

extern volatile bool need_reschedule;

struct timer* timer_create()
{
	struct timer* t = (struct timer*)kzalloc(sizeof(struct timer));
	if (t) {
		list_init(&t->list);
	}
	return t;
}

void timer_schedule(struct timer* timer,
		    u64 delay_ms,
		    timer_callback_t callback,
		    void* data)
{
	if (timer->active) {
		return; // Already scheduled
	}

	unsigned long flags;
	spin_lock_irqsave(&ts.lock, &flags);

	timer->expires_at = ts.current_ticks + millis_to_ticks(delay_ms);
	timer->callback = callback;
	timer->data = data;

	struct timer* pos;
	struct list_head* insert_point = &ts.active_timers;

	list_for_each_entry (pos, &ts.active_timers, list) {
		if (pos->expires_at > timer->expires_at) {
			insert_point = &pos->list;
			break;
		}
	}

	list_add_tail(insert_point, &timer->list);

	timer->active = true;

	spin_unlock_irqrestore(&ts.lock, flags);
}

void timer_cancel(struct timer* timer);

void timer_reschedule(struct timer* timer, u64 new_delay_ms)
{
	timer_schedule(timer, new_delay_ms, timer->callback, timer->data);
}

void timer_destroy(struct timer* timer);

void timer_tick()
{
	unsigned long flags;
	spin_lock_irqsave(&ts.lock, &flags);
	struct timer* pos = nullptr;
	struct timer* temp = nullptr;

	list_for_each_entry_safe(pos, temp, &ts.active_timers, list)
	{
		if (pos->expires_at > ts.current_ticks) {
			break; // List is sorted, so we can stop here
		}

		list_del(&pos->list);
		pos->active = false;
		spin_unlock_irqrestore(&ts.lock, flags);

		pos->callback(pos->data);

		spin_lock_irqsave(&ts.lock, &flags);
	}

	spin_unlock_irqrestore(&ts.lock, flags);
}

/**
 * Handles the timer interrupt.
 *
 * This function is called whenever the timer fires. It increments the global
 * tick count and manages the sleep countdown. Additionally, it performs an
 * action every `phase` ticks, such as updating a ticker variable.
 *
 * @param r Unused parameter representing the CPU registers at the time of the interrupt.
 */
void timer_handler(struct registers* r)
{
	(void)r;

	unsigned long flags;
	spin_lock_irqsave(&ts.lock, &flags);

	ts.current_ticks++;
	if (ts.current_ticks % phase == 0) ts.seconds_since_start++;
	if (ts.current_ticks % SCHEDULER_TIME == 0) need_reschedule = true;

	spin_unlock_irqrestore(&ts.lock, flags);

	timer_tick();
	scheduler_tick();
}

/**
 * @brief Suspends execution of the current thread for a specified duration.
 *
 * @param millis The duration to sleep, in milliseconds.
 */
void sleep(uint64_t millis)
{
	struct task* t = get_current_task();
	if (!t) return;
	// Don't need to convert to ticks since we have 1ms ticks but just incase
	t->sleep_ticks = millis_to_ticks(millis);
	yield_blocked();
}

/**
 * Sets the timer phase by configuring the Programmable Interval Timer (PIT).
 *
 * This function calculates the divisor based on the desired frequency (in Hz)
 * and programs the PIT to generate interrupts at the specified frequency.
 *
 * @param hz The desired frequency in hertz (Hz).
 */
void timer_phase(uint32_t hz)
{
	phase = hz;
	uint32_t divisor = PIT_CLK / hz; /* Calculate our divisor */
	uint8_t low = (uint8_t)(divisor & 0xFF);
	uint8_t high = (uint8_t)((divisor >> 8) & 0xFF);
	outb(0x43, 0x36);		 /* Set our command byte 0x36 */
	outb(0x40, low);		 /* Set low byte of divisor */
	outb(0x40, high);		 /* Set high byte of divisor */
}

/**
 * Initializes the system timer to generate interrupts at a frequency of 1000Hz.
 *
 * This function sets up the timer by installing the `timer_handler` as the
 * interrupt service routine (ISR) for IRQ0 and configuring the timer phase.
 * The timer is essential for task scheduling and timekeeping in the system.
 */
void timer_init(void)
{
	log_debug("Initializing timer to 1000Hz");
	/* Installs 'timer_handler' to IRQ0 */
	isr_install_handler(IRQ0, timer_handler);
	timer_phase(TIMER_HERTZ);
}
