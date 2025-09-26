/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

enum MMAP_PROT {
	PROT_NONE = 0,
	PROT_EXEC = 1 << 0,
	PROT_READ = 1 << 1,
	PROT_WRITE = 1 << 2,
};

enum MMAP_FLAGS {
	MAP_PRIVATE = 1 << 0,	/* Private mapping, copy-on-write */
	MAP_SHARED = 1 << 1,	/* Shared mapping, changes visible to others */
	MAP_ANONYMOUS = 1 << 2, /* Anonymous mapping, not backed by a file */
	MAP_GROWSDOWN = 1 << 3, /* Not supported yet :) */
};
