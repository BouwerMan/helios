#pragma once

#include <stddef.h>

void dmesg_init();
void dmesg_enqueue(const char* str, size_t len);
void dmesg_wait();
void dmesg_wake();
void dmesg_task_entry(void);

// TODO: ChatGPT made these prototypes, fill in code and rename.

// void log_task_entry(void);
// These 2 just use flag to know if it needs to log
// void wake_log_task();
// void sleep_until_log_not_empty();
