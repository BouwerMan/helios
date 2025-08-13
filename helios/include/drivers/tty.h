/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <kernel/spinlock.h>
#include <kernel/tasks/scheduler.h>
#include <sys/types.h>

// A generic ring buffer structure
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
void register_tty(struct tty* tty);
ssize_t tty_write(struct vfs_file* file, const char* buffer, size_t count);
