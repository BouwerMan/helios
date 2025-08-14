/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <drivers/fs/vfs.h>
#include <kernel/spinlock.h>
#include <kernel/tasks/scheduler.h>
#include <sys/types.h>

struct ring_buffer {
	char* buffer;
	size_t size;
	volatile size_t head; // The producer (write syscall) writes here
	volatile size_t tail; // The consumer (worker thread) reads from here
	spinlock_t lock;
	struct waitqueue
		readers; // Tasks waiting to read from the TTY (for stdin)
	struct waitqueue
		writers; // Tasks waiting to write to the TTY (if buffer is full)
};

struct tty {
	struct tty_driver* driver;
	struct list_head list;
	struct ring_buffer output_buffer;
	char name[32];
	// Add an input_buffer here later for the keyboard
};

struct tty_driver {
	// Pointer to a function that writes a string to the physical device
	ssize_t (*write)(struct tty* tty);
};

void tty_init();

/**
 * register_tty - Register a TTY device with the system
 * @tty: Pointer to the TTY device structure to register
 *
 * Adds the specified TTY device to the global list of available TTY devices.
 * This makes the TTY accessible for use by the system and applications.
 * The TTY structure must be properly initialized before calling this function.
 */
void register_tty(struct tty* tty);
ssize_t tty_write(struct vfs_file* file, const char* buffer, size_t count);
int tty_open(struct vfs_inode* inode, struct vfs_file* file);
