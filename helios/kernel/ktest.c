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
#include "drivers/console.h"
#include "drivers/serial.h"
#include "kernel/klog.h"
#include "kernel/qemu.h"
#include "kernel/types.h"
#include "lib/log.h"

extern const struct ktest __ktests_start[];
extern const struct ktest __ktests_end[];

[[noreturn]]
void ktest_run_all()
{
	log_info("\nRunning kernel tests...");
	DISABLE_INTERRUPTS();
	set_log_mode(LOG_KLOG);
	console_flush();
	klog_flush();

	u32 run = 0, failed = 0;

	for (const struct ktest* t = __ktests_start; t < __ktests_end; t++) {
		write_serial_string("test ");
		write_serial_string(t->name);
		write_serial_string(" ... ");

		run++;
		int rc = t->fn();
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
