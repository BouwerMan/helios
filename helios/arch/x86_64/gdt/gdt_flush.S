/**
 * @file arch/x86_64/gdt/gdt_flush.S
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

#include <arch/x86_64/asm.h>

#define DATA_OFFSET $(0x10)
#define CODE_OFFSET $(0x08)

FUNC(__gdt_flush)
	lgdt (%rdi)
	mov DATA_OFFSET, %ax
	
	// Load all the data segment selectors
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	mov %ax, %ss

	pop %rdi
	mov CODE_OFFSET, %rax
	push %rax
	push %rdi
	retfq

FUNC(__tss_flush)
	ltr %di
	ret

