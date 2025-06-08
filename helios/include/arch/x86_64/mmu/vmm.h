/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>

#define PML4_SIZE_PAGES 1
#define PML4_ENTRIES	512

#define PAGE_PRESENT(page) ((page) & 0x1)

/**
 * @brief Reads the value of the CR3 register.
 *
 * The CR3 register contains the physical address of the page directory base
 * register (PDBR) in x86 architecture. This function uses inline assembly
 * to retrieve the value of CR3 and return it.
 *
 * @return The value of the CR3 register as a uintptr_t.
 */
static inline uintptr_t vmm_read_cr3(void)
{
	uintptr_t cr3;
	__asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
	return cr3;
}

void vmm_init();
