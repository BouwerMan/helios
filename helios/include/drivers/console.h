/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <drivers/tty.h>

/**
 * @brief Initializes the console subsystem.
 */
void console_init();

/**
 * @brief Writes data to all attached console sinks.
 *
 * @param file VFS file handle. Unused.
 * @param buffer Source buffer containing the data to write.
 * @param count Number of bytes to write from the buffer.
 */
ssize_t console_write(struct vfs_file* file, const char* buffer, size_t count, off_t* offset);

ssize_t console_read(struct vfs_file* file, char* buffer, size_t count, off_t* offset);

/**
 * @brief Attaches a TTY device to the console output.
 *
 * @param name Name of the TTY device to attach.
 */
void attach_tty_to_console(const char* name);

/**
 * @brief Detaches a TTY device from console output.
 *
 * @param name Name of the TTY device to detach.
 */
void detach_tty(const char* name);

/**
 * @brief Flushes output buffers for all registered console sinks.
 */
void console_flush();
