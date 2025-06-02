/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>

void dmesg_init();
void dmesg_enqueue(const char* str, size_t len);
void dmesg_flush(void);
void dmesg_flush_raw(void);
void dmesg_wait();
void dmesg_wake();
void dmesg_task_entry(void);
