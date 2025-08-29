/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <drivers/tty.h>

/**
 * console_init - Initialize the console subsystem
 */
void console_init();

/**
 * console_write - Write data to all attached console sinks
 * @file: VFS file handle (unused)
 * @buffer: Source buffer containing data to write
 * @count: Number of bytes to write from the buffer
 */
ssize_t console_write(struct vfs_file* file, const char* buffer, size_t count);

/**
 * attach_tty_to_console - Attach a TTY device to the console output
 * @name: Name of the TTY device to attach
 */
void attach_tty_to_console(const char* name);

/**
 * detach_tty - Detach a TTY device from console output
 * @name: Name of the TTY device to detach
 */
void detach_tty(const char* name);

/**
 * console_flush - Flush output buffers for all registered console sinks
 */
void console_flush();
