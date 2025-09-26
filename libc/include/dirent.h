/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _DIRENT_H
#define _DIRENT_H
#pragma once

#include <helios/dirent.h>
#include <stddef.h>

#include "sys/types.h"

typedef struct __dir_stream {
	int fd;		     // File descriptor to the open directory
	struct dirent entry; // Current/next entry buffer
	off_t pos;	     // Current position in directory stream
	int error;	     // Error state (errno value)
	unsigned int flags;  // State flags (EOF, error conditions)

	char* buffer;	     // Read-ahead buffer
	size_t buf_size;     // Buffer capacity
	size_t buf_pos;	     // Current position in buffer
	ssize_t buf_valid;   // Valid data length in buffer
} DIR;

struct dirent* readdir(DIR* dirp);
DIR* opendir(const char* name);
void closedir(DIR* dirp);

#endif /* _DIRENT_H */
