#pragma once

#include <unistd.h>

extern char** __environ;

ssize_t __read(int fd, void* buf, size_t count);
ssize_t __write(int fd, const void* buf, size_t count);

int __execv(const char* path, char* const argv[]);
int __execve(const char* path, char* const argv[], char* const envp[]);
int __execvp(const char* path, char* const argv[]);
pid_t __fork(void);

int __access(const char* path, int amode);
