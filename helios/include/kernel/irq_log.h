/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>
#include <sys/types.h>

void irq_log_init();
ssize_t irq_log_write(const char* str, size_t len);
void irq_log_drain(void* data);
void irq_log_flush();
