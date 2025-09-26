/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "kernel/types.h"

static inline uint64_t __rdtsc(void)
{
	uint32_t lo, hi;

	// Serialize previous instructions to prevent reordering
	__asm__ __volatile__(
		"lfence\n" // wait for all prior instructions to complete
		"rdtsc\n"  // read time-stamp counter
		: "=a"(lo), "=d"(hi)
		:
		: "memory");

	return ((uint64_t)hi << 32) | lo;
}

extern u64 __tsc_hz;

bool tsc_is_invariant(void);
bool tsc_try_cpuid15(u64* out_hz);
// bool tsc_try_hpet(u64* out_hz);
// bool tsc_try_pmtmr(u64* out_hz);

void tsc_init(void);
