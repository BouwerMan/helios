/**
 * @file kernel/syscall.c
 *
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

#include <stddef.h>
#include <stdio.h>

#include <arch/idt.h>
#include <kernel/dmesg.h>
#include <kernel/syscall.h>

/*******************************************************************************
* Private Function Prototypes
*******************************************************************************/

[[gnu::always_inline]]
static inline void SYSRET(struct registers* r, u64 val)
{
	r->rax = val; // Set the return value
}

void sys_write(struct registers* r)
{
	// rdi: file descriptor, rsi: buffer, rdx: size
	if (r->rdi != 1) { // Only handle stdout for now
		return;
	}

	const char* buf = (const char*)r->rsi;
	size_t size = r->rdx;

	for (size_t i = 0; i < size; i++) {
		dmesg_enqueue(buf, 1);
	}
	SYSRET(r, size); // Return the number of bytes written
}

typedef void (*handler)(struct registers* r);
static const handler syscall_handlers[] = {
	0,
	sys_write,
};

static constexpr int SYSCALL_COUNT = sizeof(syscall_handlers) / sizeof(syscall_handlers[0]);

/*
 * Linux-style syscalls use specific registers to pass arguments:
 * - rax: System call number
 * - rdi: First argument
 * - rsi: Second argument
 * - rdx: Third argument
 * - r10: Fourth argument
 * - r8:  Fifth argument
 * - r9:  Sixth argument
 */
void syscall_handler(struct registers* r)
{
	if (r->rax > SYSCALL_COUNT) return;
	handler func = syscall_handlers[r->rax];
	if (func) func(r);
}

void syscall_init()
{
	isr_install_handler(SYSCALL_INT, syscall_handler);
}
