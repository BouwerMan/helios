/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <arch/regs.h>
#include <sys/types.h>

pid_t do_fork(struct registers* regs);
