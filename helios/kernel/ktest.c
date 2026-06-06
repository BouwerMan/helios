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
#include "kernel/klog.h"
#include "kernel/qemu.h"
#include "kernel/types.h"
#include "lib/log.h"

extern const struct ktest __ktests_start[];
extern const struct ktest __ktests_end[];

[[noreturn]]
void ktest_run_all()
{
	u32 run = 0, failed = 0;

	log_info("Running kernel tests...");

	for (const struct ktest* t = __ktests_start; t < __ktests_end; t++) {
		run++;
		log_info(TESTING_HEADER, t->name);
		int rc = t->fn();
		if (rc) {
			failed++;
			log_error("KTEST FAIL: %s (rc=%d)", t->name, rc);
		} else {
			log_info(TESTING_FOOTER, t->name);
		}
	}

	log_info("KTEST SUMMARY: %u run, %u failed", run, failed);

	console_flush();
	klog_flush();
	qemu_exit(failed ? QEMU_EXIT_FAILURE : QEMU_EXIT_SUCCESS);
}
