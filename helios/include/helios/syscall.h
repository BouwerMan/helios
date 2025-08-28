/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#define SYSCALL_INT 0x80

enum SyscallNumbers {
	SYS_READ = 0,
	SYS_WRITE = 1,
	SYS_OPEN = 2,
	SYS_CLOSE = 3,
	SYS_EXIT = 60,
	SYS_FORK = 57,
	SYS_EXECVE = 59,
	SYS_WAIT4 = 61,
	SYS_GETPID = 39,
	SYS_MMAP = 9,
	SYS_MUNMAP = 11,
	SYS_BRK = 12,
	SYS_GETTIMEOFDAY = 96,
	SYS_CLOCK_GETTIME = 228,
	SYS_KILL = 62,
	SYS_SIGNAL = 48,
	SYS_SIGRETURN = 15,

	SYS_TEST_COW = 123, // For testing Copy-on-Write
};
