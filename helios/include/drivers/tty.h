/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "fs/vfs.h"
#include <kernel/spinlock.h>
#include <kernel/tasks/scheduler.h>

// TODO: Actually use waitqueue

/**
 * struct ring_buffer - Circular buffer for TTY data buffering
 */
struct ring_buffer {
	char* buffer;
	size_t size;
	volatile size_t head; // The producer (write syscall) writes here
	volatile size_t tail; // The consumer (worker thread) reads from here
	spinlock_t lock;
	struct waitqueue
		readers;      // Tasks waiting to read from the TTY (for stdin)
	struct waitqueue
		writers; // Tasks waiting to write to the TTY (if buffer is full)
};

/**
 * struct tty - TTY device structure
 */
struct tty {
	struct tty_driver* driver;
	struct list_head list;
	struct ring_buffer output_buffer;
	semaphore_t write_lock;
	char name[32];
};

/**
 * struct tty_driver - TTY driver interface
 */
struct tty_driver {
	ssize_t (*write)(struct tty* tty);
};

/**
 * tty_init - Initialize the TTY subsystem
 */
void tty_init();

/**
 * register_tty - Register a TTY device with the system
 * @tty: Pointer to the TTY device structure to register
 */
void register_tty(struct tty* tty);

/**
 * tty_write - Write data to a TTY device through the VFS interface
 * @file: VFS file handle containing the TTY device in private_data
 * @buffer: Source buffer containing data to write
 * @count: Number of bytes to write from the buffer
 *
 * Return: Number of bytes successfully written to the TTY
 */
ssize_t tty_write(struct vfs_file* file, const char* buffer, size_t count);

/**
 * tty_open - Open a TTY device through the VFS interface
 * @inode: VFS inode representing the TTY device (unused)
 * @file: VFS file structure to initialize for TTY access
 *
 * Return: VFS_OK on success
 */
int tty_open(struct vfs_inode* inode, struct vfs_file* file);

/**
 * find_tty_by_name - Find a TTY device by its name
 * @name: The name of the TTY device to search for
 *
 * Return: Pointer to the TTY device if found, nullptr otherwise
 */
struct tty* find_tty_by_name(const char* name);

/**
 * __write_to_tty - Write data to a TTY device's output buffer
 * @tty: Pointer to the TTY device to write to
 * @buffer: Source buffer containing data to write
 * @count: Number of bytes to write from the buffer
 *
 * Return: Number of bytes successfully written to the output buffer
 */
ssize_t __write_to_tty(struct tty* tty, const char* buffer, size_t count);

/**
 * tty_drain_output_buffer - Work item function to drain TTY output buffer
 * @data: Void pointer to the TTY device structure to drain
 */
void tty_drain_output_buffer(void* data);
