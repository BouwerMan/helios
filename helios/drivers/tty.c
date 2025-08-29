/**
 * @file drivers/tty.c
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

#include <drivers/console.h>
#include <drivers/device.h>
#include <drivers/fs/vfs.h>
#include <drivers/serial.h>
#include <drivers/tty.h>
#include <drivers/vconsole.h>
#include <kernel/screen.h>
#include <kernel/spinlock.h>
#include <kernel/types.h>
#include <kernel/work_queue.h>
#include <lib/log.h>
#include <lib/string.h>

/*******************************************************************************
 * Global Variable Definitions
 *******************************************************************************/

static LIST_HEAD(g_ttys);

struct file_ops tty_device_fops = {
	.write = tty_write,
	.read = nullptr,
	.open = tty_open,
	.close = nullptr,
};

struct inode_ops tty_device_ops = {
	.lookup = nullptr,
};

/*******************************************************************************
 * Private Function Prototypes
 *******************************************************************************/

/**
 * tty_fill_buffer - Fill a ring buffer with data from a source buffer
 * @rb: Pointer to the ring buffer to fill
 * @buffer: Source buffer containing data to copy
 * @count: Number of bytes to copy from the source buffer
 *
 * Return: Number of bytes successfully copied to the ring buffer
 */
static ssize_t tty_fill_buffer(struct ring_buffer* rb,
			       const char* buffer,
			       size_t count);

/*******************************************************************************
 * Public Function Definitions
 *******************************************************************************/

/**
 * tty_init - Initialize the TTY subsystem
 *
 * Initializes all TTY drivers and registers their devices with the VFS.
 * This function sets up the serial and VGA console TTY devices, then
 * registers each TTY as a character device so applications can access
 * them through the filesystem. This is the main entry point for TTY
 * subsystem initialization during kernel boot.
 */
void tty_init()
{
	serial_tty_init();
	vconsole_tty_init();

	struct tty* tty = nullptr;
	list_for_each_entry (tty, &g_ttys, list) {
		register_device(tty->name, &tty_device_fops);
	}
}

/**
 * register_tty - Register a TTY device with the system
 * @tty: Pointer to the TTY device structure to register
 *
 * Adds the specified TTY device to the global list of available TTY devices.
 * This makes the TTY accessible for use by the system and applications.
 * The TTY structure must be properly initialized before calling this function.
 */
void register_tty(struct tty* tty)
{
	log_debug("Registered tty: '%s'", tty->name);
	list_add(&g_ttys, &tty->list);
}

/**
 * __write_to_tty - Write data to a TTY device's output buffer
 * @tty: Pointer to the TTY device to write to
 * @buffer: Source buffer containing data to write
 * @count: Number of bytes to write from the buffer
 *
 * Writes data to the TTY's output buffer and schedules the buffer to be
 * drained (transmitted to the actual output device). This is an internal
 * function that handles the core TTY write operation by filling the output
 * ring buffer and queuing work to process the buffered data.
 *
 * Return: Number of bytes successfully written to the output buffer
 */
ssize_t __write_to_tty(struct tty* tty, const char* buffer, size_t count)
{
	sem_wait(&tty->write_lock);

	struct ring_buffer* rb = &tty->output_buffer;

	ssize_t written = tty_fill_buffer(rb, buffer, count);

	add_work_item(tty_drain_output_buffer, tty);

	sem_signal(&tty->write_lock);

	return written;
}

/**
 * tty_write - Write data to a TTY device through the VFS interface
 * @file: VFS file handle containing the TTY device in private_data
 * @buffer: Source buffer containing data to write
 * @count: Number of bytes to write from the buffer
 *
 * Return: Number of bytes successfully written to the TTY
 */
ssize_t tty_write(struct vfs_file* file, const char* buffer, size_t count)
{
	struct tty* tty = file->private_data;

	return __write_to_tty(tty, buffer, count);
}

/**
 * tty_open - Open a TTY device through the VFS interface
 * @inode: VFS inode representing the TTY device (unused)
 * @file: VFS file structure to initialize for TTY access
 *
 * Return: VFS_OK on success
 */
int tty_open(struct vfs_inode* inode, struct vfs_file* file)
{
	(void)inode;
	file->private_data = find_tty_by_name(file->dentry->name);
	return VFS_OK;
}

/**
 * find_tty_by_name - Find a TTY device by its name
 * @name: The name of the TTY device to search for
 *
 * Return: Pointer to the TTY device if found, nullptr otherwise
 */
struct tty* find_tty_by_name(const char* name)
{
	struct tty* tty = nullptr;
	list_for_each_entry (tty, &g_ttys, list) {
		if (!strcmp(tty->name, name)) {
			return tty;
		}
	}
	return nullptr;
}

/**
 * tty_drain_output_buffer - Work item function to drain TTY output buffer
 * @data: Void pointer to the TTY device structure to drain
 *
 * This function is executed as a work item to process buffered output data
 * for a TTY device. It verifies the TTY has a valid driver with a write
 * function, then calls the driver-specific write implementation (e.g.,
 * serial_write or vconsole_write) to transmit the buffered data to the
 * actual output device.
 */
void tty_drain_output_buffer(void* data)
{
	struct tty* tty_to_drain = (struct tty*)data;

	if (tty_to_drain && tty_to_drain->driver &&
	    tty_to_drain->driver->write) {
		tty_to_drain->driver->write(tty_to_drain);
	}
}

/*******************************************************************************
 * Private Function Definitions
 *******************************************************************************/

/**
 * tty_fill_buffer - Fill a ring buffer with data from a source buffer
 * @rb: Pointer to the ring buffer to fill
 * @buffer: Source buffer containing data to copy
 * @count: Number of bytes to copy from the source buffer
 *
 * Return: Number of bytes successfully copied to the ring buffer
 */
static ssize_t tty_fill_buffer(struct ring_buffer* rb,
			       const char* buffer,
			       size_t count)
{
	size_t i = 0;
	unsigned long flags;
	spin_lock_irqsave(&rb->lock, &flags);

	// TODO: Make sure there is room

	for (; i < count; i++) {
		rb->buffer[rb->head] = buffer[i];
		rb->head = (rb->head + 1) % rb->size;

		if (rb->head == rb->tail) {
			rb->tail = (rb->tail + 1) % rb->size;
		}
	}

	spin_unlock_irqrestore(&rb->lock, flags);

	return (ssize_t)i;
}
