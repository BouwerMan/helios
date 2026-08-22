/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "drivers/device.h"
#include "fs/vfs.h"
#include "kernel/spinlock.h"
#include "kernel/tasks/scheduler.h"

// TODO: Actually use waitqueue

/**
 * @brief Circular buffer for TTY data.
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
 * @brief Represents a TTY device.
 */
struct tty {
	struct tty_driver* driver;
	struct list_head list;
	struct ring_buffer output_buffer;
	struct ring_buffer input_buffer;
	semaphore_t write_lock;
	struct chrdev chrdev;
	char name[32];
};

/**
 * @brief TTY driver interface.
 */
struct tty_driver {
	ssize_t (*write)(struct tty* tty);
	ssize_t (*read)(struct tty* tty);
	void (*input_handler)(struct tty* tty, char c); // for keyboard events
};

/**
 * @brief Initializes the TTY subsystem.
 */
void tty_init();

/**
 * @brief Registers a TTY device with the system.
 *
 * @param tty Pointer to the TTY device structure to register.
 */
void register_tty(struct tty* tty);

/**
 * @brief Writes data to a TTY device through the VFS interface.
 *
 * @param file VFS file handle with the TTY device in private_data.
 * @param buffer Source buffer containing the data to write.
 * @param count Number of bytes to write from the buffer.
 *
 * @return Number of bytes written to the TTY.
 */
ssize_t tty_write(struct vfs_file* file,
		  const char* buffer,
		  size_t count,
		  off_t* offset);

void tty_add_input_char(struct tty* tty, char c);
ssize_t
tty_read(struct vfs_file* file, char* buffer, size_t count, off_t* offset);
ssize_t __read_from_tty(struct tty* tty, char* buffer, size_t count);

/**
 * @brief Opens a TTY device through the VFS interface.
 *
 * @param inode VFS inode for the TTY device. Unused.
 * @param file VFS file structure to initialize for TTY access.
 *
 * @return VFS_OK on success.
 */
int tty_open(struct vfs_inode* inode, struct vfs_file* file);

/**
 * @brief Finds a TTY device by its name.
 *
 * @param name The name of the TTY device to search for.
 *
 * @return Pointer to the TTY device if found, nullptr otherwise.
 */
struct tty* find_tty_by_name(const char* name);

/**
 * @brief Writes data to a TTY device's output buffer.
 *
 * @param tty Pointer to the TTY device to write to.
 * @param buffer Source buffer containing the data to write.
 * @param count Number of bytes to write from the buffer.
 *
 * @return Number of bytes written to the output buffer.
 */
ssize_t __write_to_tty(struct tty* tty, const char* buffer, size_t count);

/**
 * @brief Drains a TTY device's output buffer as a work item.
 *
 * @param data Void pointer to the TTY device structure to drain.
 */
void tty_drain_output_buffer(void* data);
