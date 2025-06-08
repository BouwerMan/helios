/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>

#include <kernel/panic.h>
#include <kernel/types.h>

#define PML4_SIZE_PAGES 1
#define PML4_ENTRIES	512

#define FLAGS_MASK	0xFFFULL
#define PAGE_FRAME_MASK (~0xFFFULL)
#define PAGE_PRESENT	(1ULL << 0)  // Page is present in memory
#define PAGE_WRITE	(1ULL << 1)  // Writable
#define PAGE_USER	(1ULL << 2)  // Accessible from user-mode
#define PAGE_PWT	(1ULL << 3)  // Write-through caching enabled
#define PAGE_PCD	(1ULL << 4)  // Disable caching
#define PAGE_ACCESSED	(1ULL << 5)  // Set by CPU when page is read/written
#define PAGE_DIRTY	(1ULL << 6)  // Set by CPU on write
#define PAGE_HUGE	(1ULL << 7)  // 2 MiB or 1 GiB page (set only in PD or PDPT)
#define PAGE_PAT	(1ULL << 7)  // Page Attribute Table (set in PTE)
#define PAGE_GLOBAL	(1ULL << 8)  // Global page (ignores CR3 reload)
#define PAGE_NO_EXECUTE (1ULL << 63) // Requires EFER.NXE to be set

#define CACHE_WRITE_BACK      (0)		    // PAT=0, PCD=0, PWT=0
#define CACHE_WRITE_THROUGH   (PAGE_PWT)	    // PAT=0, PCD=0, PWT=1
#define CACHE_UNCACHABLE      (PAGE_PCD | PAGE_PWT) // PAT=0, PCD=1, PWT=1
#define CACHE_UNCACHABLE_ALT  (PAGE_PCD)	    // PAT=0, PCD=1, PWT=0
#define CACHE_WRITE_COMBINING (PAGE_PAT | PAGE_PWT) // PAT=1, PCD=0, PWT=1
#define CACHE_WRITE_PROTECTED (PAGE_PAT)	    // PAT=1, PCD=0, PWT=0

/**
 * @brief Reads the value of the CR3 register.
 *
 * The CR3 register contains the physical address of the page directory base
 * register (PDBR) in x86 architecture. This function uses inline assembly
 * to retrieve the value of CR3 and return it.
 *
 * @return The value of the CR3 register as a uintptr_t.
 */
static inline uintptr_t vmm_read_cr3()
{
	uintptr_t cr3;
	__asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
	return cr3;
}

/**
 * @brief Loads the physical address of the PML4 table into the CR3 register.
 *
 * This function sets the CR3 register to the provided physical address of the
 * PML4 table, effectively activating the page table hierarchy for virtual
 * memory management. It ensures that the provided address is 4 KiB aligned
 * before loading it into CR3.
 *
 * @param pml4_phys_addr The physical address of the PML4 table.
 *
 * @note If the provided address is not 4 KiB aligned, the function will
 *       trigger a kernel panic.
 */
static inline void vmm_load_cr3(uintptr_t pml4_phys_addr)
{
	// Ensure it's 4 KiB aligned
	kassert((pml4_phys_addr & 0xFFF) == 0 && "CR3 address must be 4 KiB aligned");

	__asm__ volatile("mov %0, %%cr3" ::"r"(pml4_phys_addr) : "memory");
}

void vmm_init();
uint64_t* vmm_create_address_space();

uint64_t* walk_page_table(uint64_t* pml4, uintptr_t vaddr, bool create, flags_t flags);
int map_page(uint64_t* pml4, uintptr_t vaddr, uintptr_t paddr, flags_t flags);
