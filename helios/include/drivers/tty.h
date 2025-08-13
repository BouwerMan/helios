/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <drivers/fs/vfs.h>
#include <sys/types.h>

void tty_init();
ssize_t tty_write(struct vfs_file* file, const char* buffer, size_t count);
