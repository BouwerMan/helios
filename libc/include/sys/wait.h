/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H
#pragma once

#include <sys/types.h>

pid_t waitpid(pid_t pid, int* stat_loc, int options);
pid_t wait(int* stat_loc);

#endif /* _SYS_WAIT_H */
