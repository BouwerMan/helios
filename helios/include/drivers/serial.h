/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <drivers/tty.h>
#include <kernel/types.h>
#include <stddef.h>
#include <sys/types.h>

static constexpr u16 COM1_PORT = 0x3f8; // COM1

int init_serial(void);
// TODO: Remove old api
void write_serial(char a);
void write_serial_string(const char* s);

void serial_tty_init();
ssize_t serial_write(struct tty* tty);
