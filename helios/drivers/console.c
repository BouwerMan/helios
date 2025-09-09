/**
 * @file drivers/console.c
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

#include "drivers/console.h"
#include "drivers/device.h"
#include "drivers/tty.h"
#include "kernel/semaphores.h"
#include "mm/kmalloc.h"

/*******************************************************************************
 * Global Variable Definitions
 *******************************************************************************/

struct file_ops console_device_fops = {
	.write = console_write,
	.read = console_read,
};

static LIST_HEAD(g_console_sinks);
static semaphore_t g_console_sem;

struct console_sink {
	struct tty* tty;
	struct list_head list;
};

/*******************************************************************************
 * Public Function Definitions
 *******************************************************************************/

/**
 * console_init - Initialize the console subsystem
 */
void console_init()
{
	sem_init(&g_console_sem, 1);
	register_device("console", &console_device_fops);
}

void attach_tty_to_console(const char* name)
{
	struct tty* tty = find_tty_by_name(name);
	if (!tty) return;

	struct console_sink* sink = kmalloc(sizeof(struct console_sink));
	if (!sink) return;

	sink->tty = tty;
	list_add_tail(&g_console_sinks, &sink->list);
}

void detach_tty(const char* name)
{
	struct tty* tty = find_tty_by_name(name);
	if (!tty) return;
	struct console_sink* sink;
	list_for_each_entry (sink, &g_console_sinks, list) {
		if (sink->tty == tty) {
			list_del(&sink->list);
			kfree(sink);
			break;
		}
	}
}

ssize_t console_write(struct vfs_file* file,
		      const char* buffer,
		      size_t count,
		      off_t* offset)
{
	(void)offset;

	sem_wait(&file->dentry->inode->lock);

	struct console_sink* sink;
	list_for_each_entry (sink, &g_console_sinks, list) {
		__write_to_tty(sink->tty, buffer, count);
	}

	sem_signal(&file->dentry->inode->lock);

	return (ssize_t)count;
}

ssize_t console_read(struct vfs_file* file,
		     char* buffer,
		     size_t count,
		     off_t* offset)
{
	(void)file;
	(void)offset;

	// TODO: Don't hardcode this
	struct tty* tty = find_tty_by_name("tty0");
	__read_from_tty(tty, buffer, count);
}

/**
 * console_flush - Flush output buffers for all registered console sinks
 */
void console_flush()
{
	struct console_sink* sink;
	list_for_each_entry (sink, &g_console_sinks, list) {
		tty_drain_output_buffer(sink->tty);
	}
}
