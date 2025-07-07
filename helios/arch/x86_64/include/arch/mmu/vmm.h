/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <mm/page.h>
#include <stdint.h>

#include <kernel/panic.h>
#include <kernel/types.h>

static constexpr int PML4_SIZE_PAGES = 1;
static constexpr int PML4_ENTRIES    = 512;

static constexpr u64 FLAGS_MASK	     = 0xFFF;
static constexpr u64 PAGE_FRAME_MASK = ~FLAGS_MASK;
static constexpr u64 PAGE_PRESENT    = 1ULL << 0;  // Page is present in memory
static constexpr u64 PAGE_WRITE	     = 1ULL << 1;  // Writable
static constexpr u64 PAGE_USER	     = 1ULL << 2;  // Accessible from user-mode
static constexpr u64 PAGE_PWT	     = 1ULL << 3;  // Write-through caching enabled
static constexpr u64 PAGE_PCD	     = 1ULL << 4;  // Disable caching
static constexpr u64 PAGE_ACCESSED   = 1ULL << 5;  // Set by CPU when page is read/written
static constexpr u64 PAGE_DIRTY	     = 1ULL << 6;  // Set by CPU on write
static constexpr u64 PAGE_HUGE	     = 1ULL << 7;  // 2 MiB or 1 GiB page (set only in PD or PDPT)
static constexpr u64 PAGE_PAT	     = 1ULL << 7;  // Page Attribute Table (set in PTE)
static constexpr u64 PAGE_GLOBAL     = 1ULL << 8;  // Global page (ignores CR3 reload)
static constexpr u64 PAGE_NO_EXECUTE = 1ULL << 63; // Requires EFER.NXE to be set

static constexpr u64 CACHE_WRITE_BACK	   = 0;			  // PAT=0, PCD=0, PWT=0
static constexpr u64 CACHE_WRITE_THROUGH   = PAGE_PWT;		  // PAT=0, PCD=0, PWT=1
static constexpr u64 CACHE_UNCACHABLE	   = PAGE_PCD | PAGE_PWT; // PAT=0, PCD=1, PWT=1
static constexpr u64 CACHE_UNCACHABLE_ALT  = PAGE_PCD;		  // PAT=0, PCD=1, PWT=0
static constexpr u64 CACHE_WRITE_COMBINING = PAGE_PAT | PAGE_PWT; // PAT=1, PCD=0, PWT=1
static constexpr u64 CACHE_WRITE_PROTECTED = PAGE_PAT;		  // PAT=1, PCD=0, PWT=0

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

static inline u64* get_pml4()
{
	return (u64*)PHYS_TO_HHDM(vmm_read_cr3());
}

/**
 * @brief Initializes the virtual memory manager (VMM).
 */
void vmm_init();
uint64_t* vmm_create_address_space();

/**
 * @brief Walks the page table hierarchy to locate or create a page table entry.
 *
 * @param pml4   Pointer to the PML4 table.
 * @param vaddr  The virtual address to resolve.
 * @param create Whether to create missing entries in the hierarchy.
 * @param flags  Flags to set for newly created entries.
 *
 * @return       A pointer to the page table entry corresponding to the given
 *               virtual address, or NULL if the entry does not exist and
 *               @create is false.
 */
uint64_t* walk_page_table(uint64_t* pml4, uintptr_t vaddr, bool create, flags_t flags);

/**
 * @brief Maps a virtual address to a physical address in the page table.
 *
 * @param pml4   Pointer to the PML4 table.
 * @param vaddr  Virtual address to map.
 * @param paddr  Physical address to map to.
 * @param flags  Flags for the page table entry (e.g., PAGE_PRESENT, PAGE_WRITE).
 *
 * @return       0 on success, -1 on failure (e.g., misalignment or mapping issues).
 */
int map_page(uint64_t* pml4, uintptr_t vaddr, uintptr_t paddr, flags_t flags);

/**
 * @brief Unmaps a virtual address from the page table.
 *
 * @param pml4   Pointer to the PML4 table.
 * @param vaddr  Virtual address to unmap.
 *
 * @return       0 on success, -1 on failure (e.g., misalignment).
 *               Returns 0 if the page was already unmapped.
 */
int unmap_page(uint64_t* pml4, uintptr_t vaddr);

/**
 * @brief Prunes empty page tables recursively.
 *
 * @param pml4   Pointer to the PML4 table.
 * @param vaddr  Virtual address to start pruning from.
 *
 * @return       0 on success.
 */
int prune_page_tables(uint64_t* pml4, uintptr_t vaddr);

void vmm_test_prune_single_mapping(void);
