/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <uapi/helios/types.h>

#define DIRENT_GET_NEXT ((off_t)(-1))

struct dirent {
	ino_t d_ino;
	off_t d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[256];
};

enum __DIRENT_TYPES {
	DT_UNKNOWN = 0,
	DT_FIFO = 1,
	DT_CHR = 2,
	DT_DIR = 4,
	DT_BLK = 6,
	DT_REG = 8,
	DT_LNK = 10,
	DT_SOCK = 12,
};
