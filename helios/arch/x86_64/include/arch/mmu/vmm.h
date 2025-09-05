/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <kernel/panic.h>
#include <kernel/types.h>
#include <mm/address_space.h>
#include <mm/page.h>
#include <mm/page_tables.h>
#include <stdint.h>

static constexpr int PML4_SIZE_PAGES = 1;
static constexpr int PML4_ENTRIES = 512;

// Page is present in memory
static constexpr u64 PAGE_PRESENT = 1ULL << 0;
// Writable
static constexpr u64 PAGE_WRITE = 1ULL << 1;
// Accessible from user-mode
static constexpr u64 PAGE_USER = 1ULL << 2;
// Write-through caching enabled
static constexpr u64 PAGE_PWT = 1ULL << 3;
// Disable caching
static constexpr u64 PAGE_PCD = 1ULL << 4;
// Set by CPU when page is read/written
static constexpr u64 PAGE_ACCESSED = 1ULL << 5;
// Set by CPU on write
static constexpr u64 PAGE_DIRTY = 1ULL << 6;
// 2 MiB or 1 GiB page (set only in PD or PDPT)
static constexpr u64 PAGE_HUGE = 1ULL << 7;
// Page Attribute Table (set in PTE)
static constexpr u64 PAGE_PAT = 1ULL << 7;
// Global page (ignores CR3 reload)
static constexpr u64 PAGE_GLOBAL = 1ULL << 8;
// Requires EFER.NXE to be set
static constexpr u64 PAGE_NO_EXECUTE = 1ULL << 63;

static constexpr u64 FLAGS_MASK = 0xFFF | PAGE_NO_EXECUTE;
static constexpr u64 PAGE_FRAME_MASK = ~FLAGS_MASK;

// PAT=0, PCD=0, PWT=0
static constexpr u64 CACHE_WRITE_BACK = 0;
// PAT=0, PCD=0, PWT=1
static constexpr u64 CACHE_WRITE_THROUGH = PAGE_PWT;
// PAT=0, PCD=1, PWT=1
static constexpr u64 CACHE_UNCACHABLE = PAGE_PCD | PAGE_PWT;
// PAT=0, PCD=1, PWT=0
static constexpr u64 CACHE_UNCACHABLE_ALT = PAGE_PCD;
// PAT=1, PCD=0, PWT=1
static constexpr u64 CACHE_WRITE_COMBINING = PAGE_PAT | PAGE_PWT;
// PAT=1, PCD=0, PWT=0
static constexpr u64 CACHE_WRITE_PROTECTED = PAGE_PAT;

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
	kassert((pml4_phys_addr & 0xFFF) == 0 &&
		"CR3 address must be 4 KiB aligned");

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

// Have to put this here so pgd_t is define :/
// #include <mm/address_space.h>

/**
 * vmm_map_anon_region - Map a memory region by allocating new pages
 * @vas: Target address space to map the region into
 * @mr: Memory region descriptor containing virtual address range and permissions
 *
 * Return: 0 on success, negative error code on failure
 * Errors: -EINVAL if vas or mr is NULL
 *         Other negative values from vmm_map_page() failures
 */
int vmm_map_anon_region(struct address_space* vas, struct memory_region* mr);

/**
 * vmm_fork_region - Fork a memory region with copy-on-write semantics
 * @dest_vas: Destination address space to create the forked region in
 * @src_mr: Source memory region to fork from
 *
 * Return: 0 on success, negative error code on failure
 * Errors: -EINVAL if dest_vas or src_mr is NULL
 *         -EFAULT if source page is not present or accessible
 *         Other negative values from vmm_map_page() or vmm_protect_page()
 */
int vmm_fork_region(struct address_space* dest_vas,
		    struct memory_region* src_mr);

/**
 * vmm_unmap_region - Unmap all pages within a memory region
 * @vas: Address space containing the memory region to unmap
 * @mr: Memory region descriptor specifying the virtual address range to unmap
 *
 * Return: 0 on success, negative error code on failure
 * Errors: Propagates error codes from vmm_unmap_page() if individual page
 *         unmapping fails (e.g., invalid virtual address, page table corruption)
 */
int vmm_unmap_region(struct address_space* vas, struct memory_region* mr);

/**
 * @brief Maps a virtual address to a physical address in the page table.
 *
 * @param pml4   Pointer to the PML4 table.
 * @param vaddr  Virtual address to map.
 * @param paddr  Physical address to map to.
 * @param flags  Flags for the page table entry (e.g., PAGE_PRESENT,
 * PAGE_WRITE).
 *
 * @return       0 on success, -1 on failure (e.g., misalignment or mapping
 * issues).
 */
int vmm_map_page(pgd_t* pml4, uintptr_t vaddr, uintptr_t paddr, flags_t flags);

/**
 * @brief Unmaps a virtual address from the page table.
 *
 * @param pml4   Pointer to the PML4 table.
 * @param vaddr  Virtual address to unmap.
 *
 * @return       0 on success, -1 on failure (e.g., misalignment).
 *               Returns 0 if the page was already unmapped.
 */
int vmm_unmap_page(pgd_t* pml4, uintptr_t vaddr);

/**
 * vmm_protect_page - Change memory protection flags for a single virtual page
 * @vas: Address space containing the page to modify
 * @vaddr: Virtual address of the page to change (must be page-aligned)
 * @new_prot: New protection flags to apply to the page
 *
 * Return: 0 on success, negative error code on failure
 * Errors: -EFAULT if the virtual address is not mapped or page is not present
 *
 * Note: The new protection flags should include appropriate architecture-specific
 *       bits (e.g., PAGE_PRESENT, PAGE_USER) as this function performs a direct
 *       flag replacement rather than selective bit modification.
 */
int vmm_protect_page(struct address_space* vas,
		     vaddr_t vaddr,
		     flags_t new_prot);

/**
 * vmm_write_region - Write data to a virtual memory region
 * @vas: Address space containing the target virtual memory
 * @vaddr: Starting virtual address to write to
 * @data: Source data buffer to copy from
 * @len: Number of bytes to write
 */
void vmm_write_region(struct address_space* vas,
		      vaddr_t vaddr,
		      const char* data,
		      size_t len);

/**
 * @brief Prunes empty page tables recursively.
 *
 * @param pml4   Pointer to the PML4 table.
 * @param vaddr  Virtual address to start pruning from.
 *
 * @return       0 on success.
 */
int prune_page_tables(uint64_t* pml4, uintptr_t vaddr);

/**
 * @brief Gets the physical address for a given virtual address.
 * @param pml4   Pointer to the PML4 table.
 * @param vaddr  Virtual address to resolve.
 * @return       The physical address corresponding to the virtual address,
 */
paddr_t get_phys_addr(pgd_t* pml4, vaddr_t vaddr);

void vmm_test_prune_single_mapping(void);
