/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <kernel/types.h>

static constexpr u16 COM1_PORT = 0x3f8; // COM1

int init_serial(void);
void write_serial(char a);
void write_serial_string(const char* s);
