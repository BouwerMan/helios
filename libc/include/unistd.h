/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _UNISTD_H
#define _UNISTD_H
#pragma once

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

int execv(const char*, char* const[]);
int execve(const char*, char* const[], char* const[]);
int execvp(const char*, char* const[]);
pid_t fork(void);

pid_t getpid(void);
pid_t getppid(void);

#ifdef __cplusplus
}
#endif

#endif /* _UNISTD_H */
