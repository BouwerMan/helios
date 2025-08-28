/**
 * @file util/log.c
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

#include <arch/idt.h>
#include <drivers/serial.h>
#include <kernel/helios.h>
#include <kernel/irq_log.h>
#include <kernel/screen.h>
#include <kernel/work_queue.h>
#include <liballoc.h>
#include <stdlib.h>
#include <string.h>
#include <util/log.h>

static constexpr char interrupt_context_str[] = LOG_COLOR_MAGENTA
	"[INT] " LOG_COLOR_RESET;

static enum LOG_MODE current_mode = LOG_DIRECT;

void set_log_mode(enum LOG_MODE mode)
{
	current_mode = mode;
}

void log_output(const char* msg, int len)
{
	extern struct vfs_file* g_kernel_console;

	switch (current_mode) {
	case LOG_DIRECT:
		write_serial_string(msg);
		screen_putstring(msg);
		break;
	case LOG_BUFFERED:
		if (unlikely(is_in_interrupt_context())) {
			irq_log_write(interrupt_context_str,
				      ARRAY_SIZE(interrupt_context_str) - 1);
			irq_log_write(msg, (size_t)len);
		} else {
			vfs_file_write(g_kernel_console, msg, (size_t)len);
		}
		break;
	}
}
