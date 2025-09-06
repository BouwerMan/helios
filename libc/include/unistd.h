/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _UNISTD_H
#define _UNISTD_H
#pragma once

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

ssize_t read(int fd, void* buf, size_t count);
ssize_t write(int fd, const void* buf, size_t count);

int execv(const char*, char* const[]);
int execve(const char* path, char* const argv[], char* const envp[]);
int execvp(const char*, char* const[]);
pid_t fork(void);

pid_t getpid(void);
pid_t getppid(void);

char* getcwd(char* buf, size_t size);
int chdir(const char* path);

#include <helios/fs.h>

int access(const char* path, int amode);

#ifdef __cplusplus
}
#endif

#endif /* _UNISTD_H */
