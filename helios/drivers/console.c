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
#include "fs/devfs/devfs.h"
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

struct chrdev console_chrdev = { 0 };

/*******************************************************************************
 * Public Function Definitions
 *******************************************************************************/

/**
 * console_init - Initialize the console subsystem
 */
void console_init()
{
	sem_init(&g_console_sem, 1);
	dev_t base;
	int e = alloc_chrdev_region(&base, 1, "console");
	if (e < 0) {
		log_error("Failed to allocate chrdev region for console: %d",
			  e);
		panic("Cannot continue without console");
	}

	console_chrdev.name = strdup("console");
	if (!console_chrdev.name) {
		log_error("Failed to allocate console chrdev name");
		panic("Cannot continue without console");
	}

	console_chrdev.base = base;
	console_chrdev.count = 1;
	console_chrdev.fops = &console_device_fops;
	console_chrdev.drvdata = nullptr;

	chrdev_add(&console_chrdev, console_chrdev.base, console_chrdev.count);

	struct vfs_superblock* devfs_sb = vfs_get_sb("/dev");
	if (!devfs_sb) {
		log_error("Failed to find devfs superblock");
		panic("Cannot continue without console");
	}

	devfs_map_name(devfs_sb,
		       console_chrdev.name,
		       console_chrdev.base,
		       FILETYPE_CHAR_DEV,
		       0666,
		       0);

	log_debug("Got sb %p for /dev", (void*)devfs_sb);
	log_debug("Console chrdev major: %u minor: %u",
		  MAJOR(console_chrdev.base),
		  MINOR(console_chrdev.base));
	log_debug("Mounted at %s/%s",
		  devfs_sb->mount_point,
		  console_chrdev.name);
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

ssize_t
console_read(struct vfs_file* file, char* buffer, size_t count, off_t* offset)
{
	(void)file;
	(void)offset;

	// TODO: Don't hardcode this
	struct tty* tty = find_tty_by_name("tty0");
	return __read_from_tty(tty, buffer, count);
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
