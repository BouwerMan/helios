/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
/*
 * x86_64 VMM (architecture-specific)
 *
 * Public API for page-table operations on x86_64 plus a small set of
 * fast-path inline helpers. Detailed contracts (Context/Locks/Return)
 * live in the corresponding .c files.
 */

#include <stdint.h>

#include "kernel/assert.h"
#include "kernel/types.h"
#include "mm/page.h"
#include "mm/page_tables.h"

/* --------------------------------------------------------------------------
 * Constants (levels, sizes)
 * -------------------------------------------------------------------------- */
enum {
	PML4_SIZE_PAGES = 1,
	PML4_ENTRIES = 512,
};

enum {
	X86_PAGE_SHIFT = 12,
	X86_PAGE_SIZE = 1ULL << X86_PAGE_SHIFT,
	X86_PAGE_MASK = ~(X86_PAGE_SIZE - 1ULL),
};

/*
 * Address field masks inside a page-table entry (4 KiB pages).
 * Physical-address bits [51:12]. Bits [62:52] are software-defined in x86;
 * bit 63 is NX (if EFER.NXE).
 */
static constexpr u64 X86_PTE_ADDR_MASK = 0x000FFFFFFFFFF000ULL;
static constexpr u64 X86_PTE_LOWFLAGS = 0x0000000000000FFFULL; /* bits 0..11 */
static constexpr u64 X86_PTE_NX = 1ULL << 63;

/* For legacy callers; prefer the X86_* names above. */
#define PTE_FLAGS_MASK (X86_PTE_LOWFLAGS | X86_PTE_NX)
#define PTE_FRAME_MASK (X86_PTE_ADDR_MASK)
/* Back-compat with older code that used these ambiguous names. */
#define FLAGS_MASK	PTE_FLAGS_MASK
#define PAGE_FRAME_MASK PTE_FRAME_MASK

/* --------------------------------------------------------------------------
 * Per-entry flag bits (common across levels unless noted)
 * -------------------------------------------------------------------------- */

/* Common flags */
static constexpr u64 PAGE_PRESENT = 1ULL << 0;
static constexpr u64 PAGE_WRITE = 1ULL << 1;
static constexpr u64 PAGE_USER = 1ULL << 2;
static constexpr u64 PAGE_PWT = 1ULL << 3; /* write-through */
static constexpr u64 PAGE_PCD = 1ULL << 4; /* cache disable */
static constexpr u64 PAGE_ACCESSED = 1ULL << 5;
static constexpr u64 PAGE_DIRTY = 1ULL << 6;
static constexpr u64 PAGE_GLOBAL = 1ULL << 8;
static constexpr u64 PAGE_NO_EXECUTE = 1ULL << 63; /* requires EFER.NXE */

/*
 * Bit 7 is overloaded by the architecture:
 * - For 4 KiB PTEs: PAT selector bit (PTE_PAT).
 * - For PDE/PDPT entries that map large pages: Page Size (PS / "huge").
 * Keep them separate to avoid misuse.
 */
static constexpr u64 PTE_PAT = 1ULL << 7; /* only meaningful in PTE */
static constexpr u64 PDE_PS = 1ULL << 7;  /* 2 MiB page when set in PDE */
static constexpr u64 PDPT_PS = 1ULL << 7; /* 1 GiB page when set in PDPT */

/* --------------------------------------------------------------------------
 * Cache-policy convenience combinations (4 KiB PTEs)
 *
 * Note: For large pages, PAT resides at a different bit position. These
 * macros are intended for 4 KiB mappings where PAT is PTE bit 7.
 * -------------------------------------------------------------------------- */

/* PAT=0, PCD=0, PWT=0 */
static constexpr u64 CACHE_WRITE_BACK = 0;
/* PAT=0, PCD=0, PWT=1 */
static constexpr u64 CACHE_WRITE_THROUGH = PAGE_PWT;
/* PAT=0, PCD=1, PWT=1 */
static constexpr u64 CACHE_UNCACHABLE = PAGE_PCD | PAGE_PWT;
/* PAT=0, PCD=1, PWT=0 */
static constexpr u64 CACHE_UNCACHABLE_ALT = PAGE_PCD;
/* PAT=1, PCD=0, PWT=1 */
static constexpr u64 CACHE_WRITE_COMBINING = PTE_PAT | PAGE_PWT;
/* PAT=1, PCD=0, PWT=0 */
static constexpr u64 CACHE_WRITE_PROTECTED = PTE_PAT;

/* --------------------------------------------------------------------------
 * CR3 helpers (arch-specific)
 * -------------------------------------------------------------------------- */

static inline paddr_t vmm_read_cr3(void)
{
	paddr_t cr3;
	__asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
	return cr3;
}

static inline void vmm_load_cr3(paddr_t pml4_phys_addr)
{
	// Ensure it's 4 KiB aligned
	kassert((pml4_phys_addr & 0xFFF) == 0,
		"CR3 address must be 4 KiB aligned");

	__asm__ volatile("mov %0, %%cr3" ::"r"(pml4_phys_addr) : "memory");
}

/* Map the current CR3 physical address into the HHDM and return a PML4 pointer. */
static inline u64* vmm_current_pml4(void)
{
	return (u64*)PHYS_TO_HHDM(vmm_read_cr3());
}

/* --------------------------------------------------------------------------
 * Opaque types
 * -------------------------------------------------------------------------- */
struct address_space;
struct memory_region;
struct page;

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/* Initialization & address-space lifecycle */
void vmm_init(void);
uint64_t* vmm_create_address_space(void);

/* Region operations */
int vmm_map_anon_region(struct address_space* vas, struct memory_region* mr);
int vmm_fork_region(struct address_space* dest_vas,
		    struct memory_region* src_mr);
int vmm_unmap_region(struct address_space* vas, struct memory_region* mr);

/* Page-granular operations */
int vmm_map_page(pgd_t* pml4, uintptr_t vaddr, uintptr_t paddr, flags_t flags);
int vmm_map_frame_alias(pgd_t* pml4,
			uintptr_t vaddr,
			uintptr_t paddr,
			flags_t flags);
int vmm_unmap_page(pgd_t* pml4, uintptr_t vaddr);

int vmm_protect_page(struct address_space* vas,
		     vaddr_t vaddr,
		     flags_t new_prot);

/* Byte-wise access into a region (slow path helper) */
void vmm_write_region(struct address_space* vas,
		      vaddr_t vaddr,
		      const void* data,
		      size_t len);

/* Maintenance / queries */
int prune_page_tables(uint64_t* pml4, uintptr_t vaddr);
paddr_t get_phys_addr(pgd_t* pml4, vaddr_t vaddr);

/* Debug/Test */
void vmm_test_prune_single_mapping(void);
