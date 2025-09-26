/**
 * rwonce.h
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2025 Dylan Parks
 *
 * This file is a derivative work based on the Linux kernel file:
 * include/asm-generic/rwonce.h
 *
 * The original file from the Linux kernel is licensed under GPL-2.0
 * (SPDX-License-Identifier: GPL-2.0) and is copyrighted by the
 * Linux kernel contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

#include "kernel/types.h"

#define compiletime_assert_rwonce_type(t)                                  \
	_Static_assert(__native_word(t) || sizeof(t) == sizeof(long long), \
		       "Unsupported access size for {READ,WRITE}_ONCE().")

#ifndef __READ_ONCE
#define __READ_ONCE(x) (*(const volatile __unqual_scalar_typeof(x)*)&(x))
#endif

#define READ_ONCE(x)                               \
	({                                         \
		compiletime_assert_rwonce_type(x); \
		__READ_ONCE(x);                    \
	})

#define __WRITE_ONCE(x, val)                        \
	do {                                        \
		*(volatile typeof(x)*)&(x) = (val); \
	} while (0)

#define WRITE_ONCE(x, val)                         \
	do {                                       \
		compiletime_assert_rwonce_type(x); \
		__WRITE_ONCE(x, val);              \
	} while (0)
