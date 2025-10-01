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

#include "kernel/assert.h"
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

static uint32_t ts_phase = 18;

extern volatile bool need_reschedule;

/**
 * timer_create() - Create and initialize a new timer
 */
struct timer* timer_create()
{
	struct timer* t = (struct timer*)kzalloc(sizeof(struct timer));
	if (t) {
		list_init(&t->list);
	}
	return t;
}

/**
 * timer_schedule() - Schedule a timer to expire after a delay
 * @timer:    Pointer to the timer to schedule
 * @delay_ms: Delay in milliseconds before the timer expires
 * @callback: Function to call when the timer expires
 * @data:     Data to pass to the callback function
 */
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

void timer_cancel(struct timer* timer)
{
	(void)timer;
	kassert(false, "Not implemented");
}

/**
 * timer_reschedule() - Reschedule an existing timer with a new delay
 * @timer: Pointer to the timer to reschedule
 * @new_delay_ms: New delay in milliseconds
 */
void timer_reschedule(struct timer* timer, u64 new_delay_ms)
{
	timer_schedule(timer, new_delay_ms, timer->callback, timer->data);
}

void timer_destroy(struct timer* timer)
{
	(void)timer;
	kassert(false, "Not implemented");
}

/**
 * timer_tick() - Check for expired timers and call their callbacks
 */
static void timer_tick(void)
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
 * timer_handler() - Called on each timer tick (from IRQ context)
 */
void timer_handler(void)
{
	// Called from IRQ context, source depends on arch (Ex: PIT)
	unsigned long flags;
	spin_lock_irqsave(&ts.lock, &flags);

	ts.current_ticks++;
	if (ts.current_ticks % ts_phase == 0) ts.seconds_since_start++;
	if (ts.current_ticks % SCHEDULER_TIME == 0) need_reschedule = true;

	spin_unlock_irqrestore(&ts.lock, flags);

	timer_tick();
	scheduler_tick();
}

void timer_init(u32 phase)
{
	ts_phase = phase;
}
