/**
 * @file helios/kernel/ktest.c
 *
 * Copyright (C) 2026  Dylan Parks
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

#include "kernel/ktest.h"
#include "arch/qemu.h"
#include "drivers/console.h"
#include "drivers/serial.h"
#include "kernel/klog.h"
#include "kernel/types.h"
#include "lib/log.h"

extern const struct ktest __ktests_start[];
extern const struct ktest __ktests_end[];

static unsigned long ktest_irq_save(void)
{
	unsigned long flags;
	__asm__ volatile("pushf; pop %0; cli" : "=r"(flags) : : "memory");
	return flags;
}
static void ktest_irq_restore(unsigned long flags)
{
	if (flags & (1UL << 9)) /* EFLAGS.IF */
		__asm__ volatile("sti" : : : "memory");
}

[[noreturn]]
void ktest_run_all()
{
	// Temporarily disable interrupts to make the output ordered in a coherent way.
	DISABLE_INTERRUPTS();

	iptr num_tests = __ktests_end - __ktests_start;
	log_info("\n\nRunning %ld kernel tests", num_tests);

	console_flush();
	klog_flush();
	klog_pause_drain(); // Stop auto emitting

	ENABLE_INTERRUPTS();

	u32 run = 0, failed = 0;

	for (const struct ktest* t = __ktests_start; t < __ktests_end; t++) {
		write_serial_string("test ");
		write_serial_string(t->name);
		write_serial_string(" ... ");

		unsigned long fl = 0;
		bool irq_off = (t->flags & KTEST_NO_PREEMPT);
		if (irq_off) fl = ktest_irq_save();
		int rc = t->fn();
		if (irq_off) ktest_irq_restore(fl);

		run++;
		if (rc == 0) {
			write_serial_string(LOG_COLOR_GREEN
					    "ok\n" LOG_COLOR_RESET);
			klog_discard_to_head(); // drop this test's captured records
		} else {
			failed++;
			write_serial_string(LOG_COLOR_RED
					    "FAILED\n" LOG_COLOR_RESET);
			klog_flush(); // replay just this test's records
		}
	}

	char buf[64];
	int n = snprintf(buf,
			 sizeof buf,
			 "\n%u passed, %u failed\n",
			 run - failed,
			 failed);
	write_serial_n(buf, (size_t)n);

	qemu_exit(failed ? QEMU_EXIT_FAILURE : QEMU_EXIT_SUCCESS);
}

static int test_ktest_self(void);
[[gnu ::used, gnu ::section(".ktests")]] static const struct ktest
	__ktest_desc_test_ktest_self = { .name = "test_ktest_self",
					 .fn = (test_ktest_self),
					 .flags = (0) };
static int test_ktest_self(void)
{
	return 0;
}
