/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>

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
