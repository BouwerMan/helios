/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#ifdef __ASSEMBLER__

#define FUNC(name)             \
	.globl name;           \
	.type name, @function; \
	name:

#endif
