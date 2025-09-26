/**
 * @file lib/log.c
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

#include "lib/log.h"
#include "drivers/serial.h"
#include "drivers/term.h"
#include "kernel/helios.h"
#include "kernel/klog.h"

static enum LOG_MODE current_mode = LOG_DIRECT;

void set_log_mode(enum LOG_MODE mode)
{
	current_mode = mode;
}

void log_output(const char* msg, int len)
{
	switch (current_mode) {
	case LOG_DIRECT:
		write_serial_string(msg);
		term_write(msg, (size_t)len);
		break;
	case LOG_KLOG:
		bool st = klog_try_write(kernel.klog,
					 KLOG_ALERT,
					 msg,
					 (u32)len,
					 nullptr);
		(void)st;
		break;
	}
}
