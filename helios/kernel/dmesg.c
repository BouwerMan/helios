/**
 * @file kernel/dmesg.c
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

#include <drivers/serial.h>
#include <kernel/dmesg.h>
#include <kernel/panic.h>
#include <kernel/screen.h>
#include <kernel/spinlock.h>
#include <kernel/tasks/scheduler.h>

#include <kernel/work_queue.h>
#include <util/log.h>

static constexpr int DMESG_BUFFER_SIZE = 0x10000;

char log_buffer[DMESG_BUFFER_SIZE];
size_t log_head = 0; // Where new messages are added
size_t log_tail = 0; // Where we consume from
spinlock_t log_lock;

struct task* dmesg_task = nullptr;

void dmesg_init()
{
	spinlock_init(&log_lock);
	// dmesg_task = new_task("DMESG task", (entry_func)dmesg_task_entry);

	log_debug("Setting log mode to use dmesg (LOG_BUFFERED)");
	set_log_mode(LOG_BUFFERED);
}

void dmesg_enqueue(const char* str, size_t len)
{
	spinlock_acquire(&log_lock);

	for (size_t i = 0; i < len; i++) {
		log_buffer[log_head] = str[i];
		log_head = (log_head + 1) % DMESG_BUFFER_SIZE;

		if (log_head == log_tail) {
			// optional: drop oldest char or pause until consumed
			log_tail = (log_tail + 1) % DMESG_BUFFER_SIZE;
		}
	}
	spinlock_release(&log_lock);

	add_work_item(dmesg_flush, nullptr);
	// dmesg_wake();
}

void dmesg_flush(void* data)
{
	spinlock_acquire(&log_lock);

	while (log_head != log_tail) {
		char c = log_buffer[log_tail];
		log_tail = (log_tail + 1) % DMESG_BUFFER_SIZE;
		spinlock_release(&log_lock);

		// output
		write_serial(c);
		screen_putchar(c);

		spinlock_acquire(&log_lock);
	}

	spinlock_release(&log_lock);
}

void dmesg_flush_raw(void)
{
	while (log_head != log_tail) {
		char c = log_buffer[log_tail];
		log_tail = (log_tail + 1) % DMESG_BUFFER_SIZE;

		write_serial(c);
		screen_putchar(c);
	}
}

// TODO: Some sort of flush mechanism at the very lest flush to serial
void dmesg_task_entry(void)
{
	while (1) {
		// dmesg_flush();
		dmesg_wait();
	}
}

static volatile bool data_ready = false;

void dmesg_wait()
{
	while (!data_ready) {
		yield_blocked();
	}
	data_ready = false;
}

void dmesg_wake()
{
	data_ready = true;
	dmesg_task->state = READY;
}
