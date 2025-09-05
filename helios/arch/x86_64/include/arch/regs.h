/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>

// Bit positions in RFLAGS
#define RFLAGS_CF    (1 << 0)  // Carry Flag - CLEAR
#define RFLAGS_FIXED (1 << 1)  // Reserved, always 1 - SET
#define RFLAGS_PF    (1 << 2)  // Parity Flag - CLEAR
#define RFLAGS_AF    (1 << 4)  // Auxiliary Flag - CLEAR
#define RFLAGS_ZF    (1 << 6)  // Zero Flag - CLEAR
#define RFLAGS_SF    (1 << 7)  // Sign Flag - CLEAR
#define RFLAGS_TF    (1 << 8)  // Trap Flag - CLEAR (single-step debug)
#define RFLAGS_IF    (1 << 9)  // Interrupt Enable - SET
#define RFLAGS_DF    (1 << 10) // Direction Flag - CLEAR
#define RFLAGS_OF    (1 << 11) // Overflow Flag - CLEAR
#define RFLAGS_IOPL  (3 << 12) // I/O Privilege Level - CLEAR (0)
#define RFLAGS_NT    (1 << 14) // Nested Task - CLEAR
#define RFLAGS_RF    (1 << 16) // Resume Flag - CLEAR
#define RFLAGS_VM    (1 << 17) // Virtual 8086 mode - CLEAR
#define RFLAGS_AC    (1 << 18) // Alignment Check - CLEAR
#define RFLAGS_VIF   (1 << 19) // Virtual Interrupt Flag - CLEAR
#define RFLAGS_VIP   (1 << 20) // Virtual Interrupt Pending - CLEAR
#define RFLAGS_ID    (1 << 21) // ID flag (CPUID available) - CLEAR

#define DEFAULT_RFLAGS (RFLAGS_FIXED | RFLAGS_IF) // 0x202

struct interrupt_context {
	uint64_t int_no, err_code;
	uint64_t rip, cs, rflags, rsp, ss;
};

// TODO: Turn this into full cpu context and split interrupt context
struct registers {
	uint64_t ds;
	// struct xmm_reg xmm[16]; // had to remove these, probably dont have them enabled
	uint64_t rdi, rsi, rbp, useless, rbx, rdx, rcx, rax;
	uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
	uint64_t saved_rflags;
	uint64_t int_no, err_code;
	uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed));
