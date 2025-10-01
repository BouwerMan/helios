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

#ifndef _UAPI_ASM_SYSCALL_H
#define _UAPI_ASM_SYSCALL_H 1

#ifdef __cplusplus
extern "C" {
#endif

#define SYSCALL_INT 0x80

enum SYSCALL_NUMBERS {
	SYS_READ,
	SYS_WRITE,
	SYS_OPEN,
	SYS_CLOSE,
	SYS_ACCESS,
	SYS_FORK,
	SYS_EXEC,
	SYS_GETCWD,
	SYS_CHDIR,
	SYS_GETDENTS,

	SYS_WAITPID,
	SYS_GETPID,
	SYS_GETPPID,
	SYS_EXIT,

	SYS_MMAP,
	// SYS_MUNMAP,
	SYS_SHUTDOWN,

	SYS_SYSCALL_COUNT,
};

#ifdef __cplusplus
}
#endif

#endif /* _UAPI_ASM_SYSCALL_H */
