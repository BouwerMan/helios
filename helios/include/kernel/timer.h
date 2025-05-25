/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>

#include "../arch/x86_64/interrupts/idt.h"

#define TIMER_HERTZ	    1000
#define millis_to_ticks(ms) ((((uint64_t)(ms) * TIMER_HERTZ) + 999) / 1000)

#define BENCHMARK_START(label) uint64_t label##_start = rdtsc()

#define BENCHMARK_END(label)            \
	uint64_t label##_end = rdtsc(); \
	log_debug(#label ": %lu (%lx) cycles", (label##_end - label##_start), (label##_end - label##_start))

static inline uint64_t rdtsc(void)
{
	uint32_t lo, hi;

	// Serialize previous instructions to prevent reordering
	__asm__ __volatile__("lfence\n" // wait for all prior instructions to complete
			     "rdtsc\n"	// read time-stamp counter
			     : "=a"(lo), "=d"(hi)
			     :
			     : "memory");

	return ((uint64_t)hi << 32) | lo;
}

void timer_init(void);
void timer_poll(void);
void timer_phase(uint32_t hz);
void sleep(uint64_t millis);
void timer_handler(struct registers* r);
