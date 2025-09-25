/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "kernel/types.h"

enum _SOFTIRQ_IDS {
	SOFTIRQ_TIMER,
	SOFTIRQ_KLOG,
};

// TODO: Return bool or just void?
typedef bool (*softirq_fn)(size_t item_budget, u64 ns_budget);

void softirq_init(void);

int softirq_register(int id, const char* name, softirq_fn fn);
void softirq_raise(int id);
// void softirq_raise_noreturn(int id);

void do_softirq(size_t item_budget, u64 ns_budget);

void try_softirq(void);
