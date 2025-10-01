/**
 * Copyright (C) 2025  Dylan Parks
 *
 * This file is part of HeliOS
 *
 * HeliOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef _UAPI_HELIOS_MMAN_H
#define _UAPI_HELIOS_MMAN_H 1

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif /* _UAPI_HELIOS_MMAN_H */
