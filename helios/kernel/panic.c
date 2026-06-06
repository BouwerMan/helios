/**
 * @file kernel/panic.c
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

#include "kernel/panic.h"
#include "arch/qemu.h"
#include "drivers/console.h"
#include "drivers/screen.h"
#include "kernel/klog.h"
#include "lib/log.h"

// Very rudimentary panic
void panic(const char* message)
{
	__asm__ volatile("cli");
	console_flush();
	klog_flush();
	set_log_mode(LOG_DIRECT);
	set_color(COLOR_RED, COLOR_BLACK);
	log_error("KERNEL PANIC!\n%s", message);
	qemu_exit(QEMU_EXIT_FAILURE);
	for (;;)
		__asm__ volatile("hlt");
}
