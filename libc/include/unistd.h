/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _UNISTD_H
#define _UNISTD_H
#pragma once

#define __need_size_t
#include <stddef.h>

#include <features.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

extern char** environ;

int close(int __fd) __nothrow;

ssize_t read(int __fd, void* __buf, size_t __count) __nothrow;
ssize_t write(int __fd, const void* __buf, size_t __count) __nothrow;

int execv(const char* __path, char* const __argv[]);
int execve(const char* __path, char* const __argv[], char* const __envp[]);
int execvp(const char* __path, char* const __argv[]);
pid_t fork(void);

pid_t getpid(void);
pid_t getppid(void);

char* getcwd(char* __buf, size_t __size);
int chdir(const char* __path);

#include <helios/fs.h>

int access(const char* __path, int __amode);

#ifndef shutdown
#define shutdown() __syscall_shutdown()
extern void __syscall_shutdown();
#endif

#ifdef __cplusplus
}
#endif

#endif /* _UNISTD_H */
