/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "arch/regs.h"
#include "arch/tsc.h"
#include "kernel/spinlock.h"
#include "kernel/time.h"
#include "kernel/types.h"
#include <stdint.h>

static constexpr int TIMER_HERTZ = 1000;
static inline unsigned long millis_to_ticks(unsigned long ms)
{
	return (ms * TIMER_HERTZ + 999) / 1000;
}

#define BENCHMARK_START(label) uint64_t label##_start = clock_now_ns()

#define BENCHMARK_END(label)                     \
	uint64_t label##_end = clock_now_ns();   \
	log_debug(#label ": %lu (%lx) ns",       \
		  (label##_end - label##_start), \
		  (label##_end - label##_start))

void timer_init(void);
void timer_poll(void);
void timer_phase(uint32_t hz);
void sleep(uint64_t millis);
void timer_handler(struct registers* r);

struct timer {
	struct list_head list;	      // Linked list node
	uint64_t expires_at;	      // Absolute time in ticks
	void (*callback)(void* data); // Function to call
	void* data;		      // User data for callback
	bool active;		      // Is this timer scheduled?
};

struct timer_subsystem {
	struct list_head active_timers; // Sorted list of active timers
	spinlock_t lock;		// Protects the timer list
	uint64_t current_ticks;		// Current system tick count
	uint64_t seconds_since_start;	// Seconds since system start
	uint32_t tick_frequency;	// Ticks per second (e.g., 1000 for 1ms)
};

typedef void (*timer_callback_t)(void* data);

struct timer* timer_create();
void timer_schedule(struct timer* timer,
		    u64 delay_ms,
		    timer_callback_t callback,
		    void* data);
void timer_cancel(struct timer* timer);
void timer_reschedule(struct timer* timer, u64 new_delay_ms);
void timer_destroy(struct timer* timer);
