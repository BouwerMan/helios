/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

// TODO: rename to panic.h or smthn

/* Types */
// #define NULL ((void*)0UL)
// typedef unsigned long uintptr_t;
// typedef long size_t;
// typedef unsigned int uint32_t;

/// Various defines
/* Kernel Strings */
#define KERNEL_NAME    "HELIOS"
#define KERNEL_VERSION "0.0.0"

__attribute__((noreturn)) void panic(const char* message);
