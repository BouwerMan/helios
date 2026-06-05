/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "arch/idt.h"
#include "arch/ports.h"
#include "kernel/helios.h"
#include "kernel/types.h"

static constexpr u8 QEMU_DEBUG_EXIT_PORT = 0xF4;

enum QEMU_EXIT_CODE {
	QEMU_EXIT_SUCCESS = 0x10,
	QEMU_EXIT_FAILURE = 0x11,
};

[[noreturn]]
static inline void qemu_exit(enum QEMU_EXIT_CODE code)
{
	outdword(QEMU_DEBUG_EXIT_PORT, code);
	DISABLE_INTERRUPTS();
	for (;;)
		halt();
}

#define QEMU_BREAKPOINT (__asm__ volatile("jmp $"))

// outword is QEMU ACPI shutdown method
// outb is the QEMU ISA shutdown method
// This shutdown method probably shouldn't be used.
#define QEMU_SHUTDOWN()                 \
	({                              \
		outword(0x604, 0x2000); \
		outb(0xF4, 0);          \
	})
