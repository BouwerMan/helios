/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "kernel/types.h"

typedef enum SOFTIRQ_RET {
	SOFTIRQ_DONE = 0,
	SOFTIRQ_MORE,
	SOFTIRQ_PUNT,
} softirq_ret_t;

enum SOFTIRQ_IDS {
	SOFTIRQ_TIMER,
	SOFTIRQ_KLOG,
	NUM_SOFTIRQS,
};

// TODO: Return bool or just void?
typedef softirq_ret_t (*softirq_fn)(size_t item_budget, u64 ns_budget);

struct softirq {
	const char* name;
	softirq_fn fn;
};

void softirq_init(void);

int softirq_register(int id, const char* name, softirq_fn fn);
void softirq_raise(int id);
// void softirq_raise_noreturn(int id);

void do_softirq(size_t item_budget, u64 ns_budget);

void try_softirq(void);
