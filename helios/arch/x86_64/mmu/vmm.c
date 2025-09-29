/**
 * @file arch/x86_64/mmu/vmm.c
 *
 * Copyright (C) 2025  Dylan Parks
 *
 * This file is part of HeliOS
 *
 * HeliOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
* Unlike my original vmm implementation, this one only focuses on paging and address space magaement.
* Overview:
* 	1. Kernel inits bootmem
* 	2. Kernel inits page_alloc
* 	3. Kernel decommissions bootmem which then releases limine reclaimable resources
* 	4. We init our kernel address space
*
* We will have a mapping of the entire physical memory space at hhdm_offset.
*/

#include "kernel/spinlock.h"
#include "mm/kmalloc.h"
#include <stddef.h>
#include <stdint.h>
#include <uapi/helios/errno.h>
#include <uapi/helios/mman.h>

#undef LOG_LEVEL
#define LOG_LEVEL 1
#define FORCE_LOG_REDEF
#include <lib/log.h>
#undef FORCE_LOG_REDEF

#include "arch/idt.h"
#include "arch/mmu/vmm.h"
#include "arch/regs.h"
#include "drivers/console.h"
#include "kernel/bootinfo.h"
#include "kernel/helios.h"
#include "kernel/irq_log.h"
#include "kernel/klog.h"
#include "kernel/panic.h"
#include "kernel/tasks/scheduler.h"
#include "lib/string.h"
#include "mm/address_space.h"
#include "mm/address_space_dump.h"
#include "mm/page.h"
#include "mm/page_alloc.h"

extern char __kernel_start[], __kernel_end[];

/*******************************************************************************
* Private Function Prototypes
*******************************************************************************/

static void page_fault(struct registers* r);

[[noreturn]] static void page_fault_fail(struct registers* r);

static int do_demand_paging(struct registers* r);

static pte_t*
walk_page_table(pgd_t* pml4, vaddr_t vaddr, bool create, flags_t flags);

static void map_memmap_entry(pgd_t* pml4,
			     struct bootinfo_memmap_entry* entry,
			     uptr k_vstart,
			     uptr k_pstart,
			     size_t k_size);

static void log_page_table_walk(u64* pml4, vaddr_t vaddr);

static bool is_table_empty(pgd_t* table);

static bool
prune_page_table_recursive(uint64_t* table, int level, uintptr_t vaddr);

/*******************************************************************************
 * Private inline helpers
 ******************************************************************************/

/**
 * invalidate - Invalidate a single TLB entry with invlpg
 * @vaddr: Virtual address whose translation to invalidate
 *
 * Context: Local CPU only; does not broadcast. Callers handle shootdowns.
 */
static inline void invalidate(vaddr_t vaddr)
{
	__asm__ volatile("invlpg (%0)" ::"r"(vaddr) : "memory");
}

/**
 * _page_table_index - Extract 9-bit index for a page-table level
 * @vaddr: Virtual address
 * @shift: Bit position of the level's index (39, 30, 21, 12)
 * Return: Index in range [0, 511]
 */
static inline size_t _page_table_index(vaddr_t vaddr, int shift)
{
	return (vaddr >> shift) & 0x1FF;
}

#define _pml4_index(vaddr) _page_table_index(vaddr, 39)
#define _pdpt_index(vaddr) _page_table_index(vaddr, 30)
#define _pd_index(vaddr)   _page_table_index(vaddr, 21)
#define _pt_index(vaddr)   _page_table_index(vaddr, 12)

/**
 * get_table_index - Extract 9-bit page-table index at a given level
 * @level: Walk level (0=PML4, 1=PDPT, 2=PD, 3=PT)
 * @vaddr: Virtual address to decode
 * Return: Index in range [0, 511]
 * Context: Pure; does not sleep; IRQ-safe; no locks.
 *
 * Computes the level-specific index used during a page-table walk. The
 * caller must ensure @level is valid for the current paging mode.
 */
static inline size_t get_table_index(int level, uintptr_t vaddr)
{
	/* Levels: 0=PML4, 1=PDPT, 2=PD, 3=PT */
	kassert((unsigned)level <= 3, "bad pt level");
	return (vaddr >> (39 - 9 * level)) & 0x1FF;
}

/**
 * _alloc_page_table - Allocate a single 4 KiB page-table frame
 * @flags: Allocation flags for low-level page allocator
 * Return: Zeroed memory sized for one page-table
 *
 * Note: New page tables must be zeroed per x86 paging rules. If the
 * allocator does not guarantee zeroed pages, ensure caller clears it.
 */
static inline void* _alloc_page_table(aflags_t flags)
{
	return (void*)get_free_pages(flags, PML4_SIZE_PAGES);
}

/**
 * _free_page_table - Free a single 4 KiB page-table frame
 * @table: Pointer previously returned by _alloc_page_table()
 */
static inline void _free_page_table(void* table)
{
	free_pages(table, PML4_SIZE_PAGES);
}

/**
 * flags_from_mr - Translate region protection to x86 PTE flags
 * @mr: Memory region descriptor
 * Return: Initial PTE flags for leaf mappings
 *
 * Policy: Presence is decided by the mapper; user bit is controlled by
 * region flags (uncomment when wired). NX is set when !EXEC.
 */
static inline flags_t flags_from_mr(struct memory_region* mr)
{
	flags_t flags = PAGE_PRESENT | PAGE_USER;

	if (mr->prot & PROT_READ) flags |= PAGE_PRESENT;
	if (mr->prot & PROT_WRITE) flags |= PAGE_WRITE;
	if (!(mr->prot & PROT_EXEC)) flags |= PAGE_NO_EXECUTE;
	// if (mr->flags & MAP_USER) flags |= PAGE_USER;

	return flags;
}

/*******************************************************************************
* Public Function Definitions
*******************************************************************************/

/**
 * vmm_init - Initialize paging and the kernel address space
 * Return: none
 * Context: Early boot on the BSP; non-preemptible; IRQ state unspecified.
 *
 * Sets up the kernel's top-level page table, maps regions described by the
 * boot info, installs the page-fault handler, and activates the new tables.
 */
void vmm_init()
{
	isr_install_handler(PAGE_FAULT, page_fault);

	// Init new address space, then copy from limine
	struct bootinfo* bootinfo = &kernel.bootinfo;
	if (!bootinfo->valid) panic("bootinfo marked not valid");

	uptr k_vstart = align_down_page((uptr)&__kernel_start);
	uptr k_vend = align_up_page((uptr)&__kernel_end);
	size_t kernel_size = k_vend - k_vstart;

	if (k_vstart != bootinfo->executable.virtual_base) {
		panic("Kernel address range does not match bootinfo");
	}

	uptr k_pstart = bootinfo->executable.physical_base;

	kernel.pml4 = _alloc_page_table(AF_KERNEL);
	log_debug("Current PML4: %p", (void*)kernel.pml4);
	for (size_t i = 0; i < bootinfo->memmap_entry_count; i++) {
		struct bootinfo_memmap_entry* entry = &bootinfo->memmap[i];
		map_memmap_entry((pgd_t*)kernel.pml4,
				 entry,
				 k_vstart,
				 k_pstart,
				 kernel_size);
	}

	vmm_load_cr3(HHDM_TO_PHYS(kernel.pml4));
}

/**
 * vmm_create_address_space - Allocate a fresh top-level page table
 * Return: Pointer to a new PML4 initialized from the kernel template.
 * Context: May sleep depending on allocator; IRQ-safe; no locks held.
 *
 * Allocates a PML4 and seeds it from the current kernel address-space
 * template. Panics on out-of-memory.
 */
uint64_t* vmm_create_address_space()
{
	// pml4 has 512 entries, each 8 bytes. which means it is 4096 (1 page) bytes in size.
	uint64_t* pml4 = _alloc_page_table(AF_KERNEL);
	if (!pml4) {
		log_error("Failed to allocate PML4");
		panic("Out of memory");
	}

	memcpy(pml4, kernel.pml4, PAGE_SIZE);
	log_info("Created address space with PML4 at %p", (void*)pml4);

	return pml4;
}

/**
 * vmm_map_page - Install a PRESENT PTE and take the mapping pin
 * @pml4:   page-table root
 * @vaddr:  page-aligned virtual address (must be unmapped)
 * @paddr:  page-aligned physical address to map
 * @flags:  PTE flags (USER/WRITE/PRESENT/NX/etc.)
 *
 * Creates a new mapping for @vaddr to @paddr with @flags. Fails if a PRESENT
 * PTE already exists. Takes exactly one mapping reference (get_page) on the
 * mapped frame on success. Must not sleep; caller is responsible for higher-
 * level policy/locking.
 *
 * Return: 0 on success; -EINVAL on misalignment; -EFAULT if PTE already
 *         present or walk failed; other -errno as implemented.
 */
int vmm_map_page(pgd_t* pml4, uintptr_t vaddr, uintptr_t paddr, flags_t flags)
{
	if (!is_page_aligned(vaddr) || !is_page_aligned(paddr)) {
		log_error(
			"Something isn't aligned right, vaddr: %lx, paddr: %lx",
			vaddr,
			paddr);
		return -EINVAL;
	}

	// We want PAGE_PRESENT and PAGE_WRITE on almost all the higher levels
	flags_t walk_flags = flags & (PAGE_USER | PAGE_PRESENT | PAGE_WRITE);
	pte_t* pte = walk_page_table(pml4,
				     vaddr,
				     true,
				     walk_flags | PAGE_PRESENT | PAGE_WRITE);

	if (!pte || pte->pte & PAGE_PRESENT) {
		log_warn("Could not find pte or pte is already present");
		return -EFAULT;
	}

	pte->pte = paddr | flags;
	struct page* page = phys_to_page(pte->pte & PAGE_FRAME_MASK);
	get_page(page); // Reference for the mapping
	map_page(page);

	return 0;
}

/**
 * vmm_map_frame_alias - Map a PA at VA without owning a page ref
 * @pml4:   page-table root
 * @vaddr:  page-aligned virtual address
 * @paddr:  page-aligned physical address
 * @flags:  PTE flags
 *
 * Creates a non-owning alias mapping (no get_page / mapcount changes).
 * Intended for HHDM, identity maps, and MMIO.
 *
 * Return: 0 on success; -EINVAL on misalignment; -EFAULT if already mapped.
 */
int vmm_map_frame_alias(pgd_t* pml4,
			uintptr_t vaddr,
			uintptr_t paddr,
			flags_t flags)
{
	if (!is_page_aligned(vaddr) || !is_page_aligned(paddr)) {
		log_error(
			"Something isn't aligned right, vaddr: %lx, paddr: %lx",
			vaddr,
			paddr);
		return -EINVAL;
	}

	// We want PAGE_PRESENT and PAGE_WRITE on almost all the higher levels
	flags_t walk_flags = flags & (PAGE_USER | PAGE_PRESENT | PAGE_WRITE);
	pte_t* pte = walk_page_table(pml4,
				     vaddr,
				     true,
				     walk_flags | PAGE_PRESENT | PAGE_WRITE);

	if (!pte) {
		log_warn("Could not find pte, vaddr: %lx, paddr: %lx",
			 vaddr,
			 paddr);
		return -EFAULT;
	}
	if (pte->pte & PAGE_PRESENT) {
		log_warn(
			"PTE already present, vaddr: %lx, paddr: %lx, pte: %lx",
			vaddr,
			paddr,
			pte->pte);
		return -EFAULT;
	}

	pte->pte = paddr | flags;

	return 0;
}

/**
 * vmm_unmap_page - Remove a PRESENT PTE and drop the mapping pin
 * @pml4:   page-table root
 * @vaddr:  page-aligned virtual address
 *
 * Idempotently clears a PRESENT PTE at @vaddr (if any), drops exactly one
 * mapping reference (put_page) on the mapped frame, prunes now-empty page-table
 * levels, and invalidates the TLB for @vaddr.
 *
 * Return: 0 on success (including “already unmapped”); -EINVAL on misalignment.
 */
int vmm_unmap_page(pgd_t* pml4, uintptr_t vaddr)
{
	if (!is_page_aligned(vaddr)) {
		log_error("Something isn't aligned right, vaddr: %lx", vaddr);
		return -EINVAL;
	}

	pte_t* pte = walk_page_table(pml4, vaddr, false, 0);

	if (!pte || !(pte->pte & PAGE_PRESENT)) {
		return 0; // Already unmapped, nothing to do
	}

	struct page* page = phys_to_page(pte->pte & PAGE_FRAME_MASK);
	unmap_page(page);
	put_page(page);

	pte->pte = 0;

	// TODO: Rework this to use the new typedefs (I am lazy)
	prune_page_tables((uint64_t*)pml4, vaddr);
	invalidate(vaddr);

	return 0;
}

/**
 * prune_page_tables - Free empty page-table nodes under an address
 * @pml4: Top-level page table to prune
 * @vaddr: Virtual address whose walk anchors the prune
 * Return: 0
 * Context: May sleep. IRQs enabled. Caller must synchronize page-table access.
 *
 * Recursively drops intermediate page-table levels that contain no present
 * entries along the walk rooted at @vaddr. Does not change leaf mappings and
 * does not perform TLB shootdowns; callers handle any required invalidation.
 */
int prune_page_tables(uint64_t* pml4, uintptr_t vaddr)
{
	(void)prune_page_table_recursive(pml4, 0, vaddr);

	return 0;
}

/**
 * Test function to validate the pruning of a single mapping in the page table.
 *
 * Steps:
 * 1. Allocates a fresh address space (PML4 table).
 * 2. Maps a test virtual address to a physical page.
 * 3. Unmaps the virtual address.
 * 4. Prunes the page tables to remove unused entries.
 * 5. Verifies that the PML4 entry is cleared after pruning.
 * 6. Cleans up allocated resources.
 *
 * This function logs the success or failure of each step and ensures that
 * the page table pruning logic works as expected.
 */
void vmm_test_prune_single_mapping(void)
{
	// 1. Allocate a fresh address space
	uint64_t* pml4 = _alloc_page_table(AF_KERNEL);

	// 2. Choose a test virtual address and physical page
	uintptr_t vaddr = 0x00007FFFFFFFE000; // Arbitrary, canonical, aligned
	uintptr_t paddr = (uintptr_t)HHDM_TO_PHYS(get_free_page(AF_KERNEL));

	log_info("Mapping page: virt=0x%lx -> phys=0x%lx", vaddr, paddr);
	int result = vmm_map_page((pgd_t*)pml4,
				  vaddr,
				  paddr,
				  PAGE_PRESENT | PAGE_WRITE | CACHE_WRITE_BACK);
	if (result != 0) {
		log_error("Failed to map test page");
		return;
	}

	// 3. Unmap the virtual address
	log_info("Unmapping page: 0x%lx", vaddr);
	result = vmm_unmap_page((pgd_t*)pml4, vaddr);
	if (result != 0) {
		log_error("Failed to unmap test page");
		return;
	}

	// 4. Prune page tables
	log_info("Pruning page tables for vaddr 0x%lx", vaddr);
	prune_page_tables(pml4, vaddr);

	// 5. Verify that the PML4 entry is now 0
	size_t pml4_i = get_table_index(0, vaddr);
	if (pml4[pml4_i] == 0) {
		log_info("✅ PML4 entry cleared — pruning successful");
	} else {
		log_error("❌ PML4 entry still set: 0x%lx", pml4[pml4_i]);
	}

	// 6. Cleanup
	free_page((void*)PHYS_TO_HHDM(paddr));
	_free_page_table(pml4);
}

/**
 * get_phys_addr - Resolve a virtual address to a physical address
 * @pml4: Top-level page table used for the walk
 * @vaddr: Virtual address to translate
 * Return: Physical address on success, 0 if unmapped or not present
 * Context: Does not sleep; IRQ-safe; no locks. No TLB changes.
 *
 * Performs a non-allocating walk (create=false) to find the leaf PTE for
 * @vaddr. If present, returns the PTE frame address plus the page offset.
 * No access checks (user/supervisor) are performed here.
 */
paddr_t get_phys_addr(pgd_t* pml4, vaddr_t vaddr)
{
	u64 low = vaddr & (X86_PAGE_SIZE - 1); /* was: X86_PTE_LOWFLAGS */

	pte_t* pte = walk_page_table(pml4, vaddr & X86_PTE_ADDR_MASK, false, 0);
	if (!pte || !(pte->pte & PAGE_PRESENT)) {
		return 0;
	}

	paddr_t paddr = pte->pte & X86_PTE_ADDR_MASK;
	return paddr + low;
}

/**
 * vmm_map_anon_region - Map a region by allocating fresh pages
 * @vas: Target address space
 * @mr:  Region with [start,end) and protections
 * Return: 0 or -errno
 * Context: May sleep. Locks: acquires @vas->vma_lock (read) and @vas->pgt_lock.
 * Notes: Maps one zeroed page per PTE using flags from @mr->prot. On failure,
 *        unmaps pages created by this call.
 */
int vmm_map_anon_region(struct address_space* vas, struct memory_region* mr)
{
	if (!vas || !mr) {
		return -EINVAL;
	}

	kassert(mr->kind == MR_ANON);

	int err = 0;

	vaddr_t v = mr->start;
	for (; v < mr->end; v += PAGE_SIZE) {
		struct page* page = alloc_zeroed_page(AF_NORMAL); // may sleep
		if (!page) {
			err = -ENOMEM;
			goto clean;
		}

		/*
 		 * Double check region didn't move while allocating
 		 */
		down_read(&vas->vma_lock);
		if (!is_within_region(mr, v)) {
			up_read(&vas->vma_lock);
			put_page(page); // Drop build ref
			err = -EFAULT;
			goto clean;
		}

		flags_t flags = flags_from_mr(mr);

		paddr_t paddr = page_to_phys(page);

		unsigned long irqf;
		spin_lock_irqsave(&vas->pgt_lock, &irqf);

		// Mapped by someone else?
		if (get_phys_addr(vas->pml4, v)) {
			spin_unlock_irqrestore(&vas->pgt_lock, irqf);
			up_read(&vas->vma_lock);
			put_page(page); // Drop build ref
			continue;
		}

		err = vmm_map_page(vas->pml4,
				   v,
				   paddr,
				   flags); // must not sleep

		spin_unlock_irqrestore(&vas->pgt_lock, irqf);
		up_read(&vas->vma_lock);

		put_page(page); // drop build ref regardless
		if (err < 0) {
			goto clean;
		}
	}

	return 0;

clean:
	for (vaddr_t u = mr->start; u < v; u += PAGE_SIZE) {
		unsigned long spinflags;
		spin_lock_irqsave(&vas->pgt_lock, &spinflags);
		(void)vmm_unmap_page(vas->pml4, u);
		spin_unlock_irqrestore(&vas->pgt_lock, spinflags);
	}
	return err;
}

/**
 * vmm_fork_region - Mirror a region into @dest_vas (COW for private)
 * @dest_vas: Destination address space (child)
 * @src_mr:   Source region in its owner address space (parent)
 * Return: 0 or -errno (-ENOTSUP for devices, -ENOMEM on alloc failure)
 * Context: May sleep. Locks: takes @src_vas/@dest_vas vma read locks; uses
 *          page-table locks around walks/updates.
 * Notes: Present pages are mapped into @dest_vas. For private regions, clears
 *        PAGE_WRITE in both parent and child to arm COW. Non-present pages are
 *        skipped (handled by demand paging later).
 */
int vmm_fork_region(struct address_space* dest_vas,
		    struct memory_region* src_mr)
{
	int err = 0;
	int out_err = 0;
	vaddr_t v = 0;

	if (!dest_vas || !src_mr) {
		return -EINVAL;
	}
	if (src_mr->kind == MR_DEVICE) {
		return -ENOTSUP;
	}

	struct address_space* src_vas = src_mr->owner;
	if (dest_vas == src_vas) {
		return -EINVAL;
	}

	down_read(&dest_vas->vma_lock);
	down_read(&src_vas->vma_lock);

	size_t num_pages = (src_mr->end - src_mr->start) >> PAGE_SHIFT;
	// temporary guard: 4GB limit @ 4K pages
	if (num_pages > (1UL << 20)) {
		up_read(&src_vas->vma_lock);
		up_read(&dest_vas->vma_lock);
		return -ENOMEM;
	}

	size_t prot_idx = 0;
	bool* protected = kzalloc(num_pages);
	if (!protected) {
		up_read(&src_vas->vma_lock);
		up_read(&dest_vas->vma_lock);
		return -ENOMEM;
	}

	memset(protected, 0, num_pages);

	for (v = src_mr->start; v < src_mr->end; v += PAGE_SIZE, prot_idx++) {
		unsigned long irqf;
		spin_lock_irqsave(&src_vas->pgt_lock, &irqf);
		pte_t* src_pte = walk_page_table(src_vas->pml4, v, false, 0);
		u64 snapshot = src_pte ? src_pte->pte : 0;
		spin_unlock_irqrestore(&src_vas->pgt_lock, irqf);

		if (!(snapshot & PAGE_PRESENT)) continue; // demand-paged later

		bool priv = src_mr->is_private;
		paddr_t p = src_pte->pte & X86_PTE_ADDR_MASK;
		flags_t current_flags = src_pte->pte &
					(X86_PTE_LOWFLAGS | X86_PTE_NX);
		flags_t new_flags = priv ? (current_flags & ~PAGE_WRITE) :
					   current_flags;

		/* Map into child */
		spin_lock_irqsave(&src_vas->pgt_lock, &irqf);
		err = vmm_map_page(dest_vas->pml4, v, p, new_flags);
		spin_unlock_irqrestore(&src_vas->pgt_lock, irqf);
		if (err < 0) {
			out_err = err;
			goto clean;
		}

		// Skip write protecting if page is already read only
		if (priv && (current_flags & PAGE_WRITE)) {
			err = vmm_protect_page(src_vas, v, new_flags);
			if (err < 0) {
				out_err = err;
				goto clean;
			}
			protected[prot_idx] = true;
		}
	}

	kfree(protected);
	up_read(&src_vas->vma_lock);
	up_read(&dest_vas->vma_lock);
	return 0;

clean:
	log_error("Failed to fork region: %d", out_err);

	vaddr_t cleanup_end = v; // Don't include the failed page
	prot_idx = 0;
	for (v = src_mr->start; v < cleanup_end; v += PAGE_SIZE, prot_idx++) {
		unsigned long irqf;
		spin_lock_irqsave(&src_vas->pgt_lock, &irqf);

		// Find the original flags to restore them
		pte_t* src_pte = walk_page_table(src_vas->pml4, v, false, 0);
		spin_unlock_irqrestore(&src_vas->pgt_lock, irqf);

		if (src_pte && (src_pte->pte & PAGE_PRESENT)) {
			flags_t original_flags = (src_pte->pte & FLAGS_MASK) |
						 PAGE_WRITE;
			// Restore parent write permissions if we removed them
			if (protected[prot_idx]) {
				vmm_protect_page(src_vas, v, original_flags);
			}
		}

		spin_lock_irqsave(&src_vas->pgt_lock, &irqf);
		int res = vmm_unmap_page(dest_vas->pml4, v);
		spin_unlock_irqrestore(&src_vas->pgt_lock, irqf);

		if (res < 0) {
			panic("Could not cleanup vmm_fork_region");
		}
	}

	kfree(protected);
	up_read(&src_vas->vma_lock);
	up_read(&dest_vas->vma_lock);
	return err;
}

/**
 * vmm_unmap_region - Remove all mappings within a region
 * @vas: Address space owning the mappings
 * @mr:  Region with [start,end) to unmap
 * Return: 0 or -errno from vmm_unmap_page()
 * Context: May sleep. Locks: acquires @vas->vma_lock (read) and @vas->pgt_lock.
 * Notes: Drops PTEs; underlying page freeing follows separate refcount policy.
 */
int vmm_unmap_region(struct address_space* vas, struct memory_region* mr)
{
	down_read(&vas->vma_lock);

	for (vaddr_t v = mr->start; v < mr->end; v += PAGE_SIZE) {
		unsigned long spinflags;
		spin_lock_irqsave(&vas->pgt_lock, &spinflags);
		int err = vmm_unmap_page(vas->pml4, v);
		spin_unlock_irqrestore(&vas->pgt_lock, spinflags);

		if (err < 0) {
			up_read(&vas->vma_lock);
			return err;
		}
	}

	up_read(&vas->vma_lock);
	return 0;
}

/**
 * vmm_protect_page - Replace PTE permission bits for one page
 * @vas:      Address space
 * @vaddr:    Page-aligned virtual address
 * @new_prot: New flags (include PRESENT/USER as appropriate)
 * Return: 0 or -errno (-EINVAL bad @vas, -EFAULT unmapped/not present)
 * Context: Does not sleep. Locks: takes @vas->pgt_lock (IRQs disabled inside).
 * Notes: Preserves frame address; updates only flags and invalidates local TLB.
 */
int vmm_protect_page(struct address_space* vas, vaddr_t vaddr, flags_t new_prot)
{
	if (!vas) return -EINVAL;

	unsigned long spinflags;
	spin_lock_irqsave(&vas->pgt_lock, &spinflags);

	pte_t* pte = walk_page_table(vas->pml4, vaddr, false, 0);
	if (!pte || !(pte->pte & PAGE_PRESENT)) {
		spin_unlock_irqrestore(&vas->pgt_lock, spinflags);
		return -EFAULT;
	}

	uptr paddr = pte->pte & X86_PTE_ADDR_MASK;
	pte->pte = paddr | (new_prot & (X86_PTE_LOWFLAGS | X86_PTE_NX));

	invalidate(vaddr);

	spin_unlock_irqrestore(&vas->pgt_lock, spinflags);
	return 0;
}

/**
 * vmm_install_page - Finalize mapping of a prepared page into a VMA
 * @vas:    target address space (owns @mr)
 * @mr:     covering memory_region in @vas
 * @vaddr:  page-aligned virtual address within @mr
 * @page:   page with a build ref (ref > 0); content already prepared
 *
 * Acquires @vas->vma_lock (read) and @vas->pgt_lock, then installs a PRESENT
 * PTE for @vaddr. For MR_FILE|MAP_PRIVATE, WRITE is cleared to arm CoW.
 * If a mapping already exists, succeeds iff it maps the same frame.
 *
 * Return: 0 on success or identical existing mapping; -EEXIST if mapped to a
 *         different frame; -ENOTSUP for MR_DEVICE; -EFAULT if @vaddr not in
 *         @mr; -EINVAL on bad args; other errors from vmm_map_page().
 *
 * Refcounting: Does NOT touch the caller’s build ref. vmm_map_page() takes
 *              the mapping pin on success; caller should put_page(@page) after.
 */
int vmm_install_page(struct address_space* vas,
		     struct memory_region* mr,
		     vaddr_t vaddr,
		     struct page* page)
{
	if (!vas || !mr || !page || mr->owner != vas ||
	    !is_page_aligned(vaddr)) {
		return -EINVAL;
	}

	kassert(atomic_read(&page->ref_count) > 0);

	down_read(&vas->vma_lock);
	if (mr->kind == MR_DEVICE) {
		up_read(&vas->vma_lock);
		return -ENOTSUP;
	}
	if (vaddr < mr->start || vaddr >= mr->end) {
		up_read(&vas->vma_lock);
		return -EFAULT;
	}

	unsigned long spinflags;
	spin_lock_irqsave(&vas->pgt_lock, &spinflags);

	// Check for race condition where page got mapped already
	paddr_t existing = get_phys_addr(vas->pml4, vaddr);
	if (existing) {
		spin_unlock_irqrestore(&vas->pgt_lock, spinflags);
		up_read(&vas->vma_lock);

		// Can be a success if we get the same physical address
		return (existing == page_to_phys(page)) ? 0 : -EEXIST;
	}

	bool is_file = (mr->kind == MR_FILE);

	flags_t flags = flags_from_mr(mr);
	if (is_file && mr->is_private) {
		// Private file mappings are COW -> remove WRITE to start
		flags &= ~PAGE_WRITE;
	}

	paddr_t paddr = page_to_phys(page);
	int err = vmm_map_page(vas->pml4, vaddr, paddr, flags);
	if (err < 0) {
		spin_unlock_irqrestore(&vas->pgt_lock, spinflags);
		up_read(&vas->vma_lock);
		return err;
	}

	spin_unlock_irqrestore(&vas->pgt_lock, spinflags);
	up_read(&vas->vma_lock);
	return 0;
}

/**
 * __vmm_populate_one_anon - Prepare and map one anonymous page
 * @vas:    target address space
 * @mr:     anonymous memory region covering @vaddr
 * @vaddr:  virtual address (any offset within the page)
 *
 * Allocates a zeroed page (build ref), then calls vmm_install_page() to map it.
 * Always drops the build ref before returning.
 *
 * Return: 0 on success; -ENOMEM on OOM; <0 on install failure.
 *
 * Context: may sleep; no I/O.
 */
int __vmm_populate_one_anon(struct address_space* vas,
			    struct memory_region* mr,
			    vaddr_t vaddr)
{
	if (!vas || !mr) return -EINVAL;

	vaddr_t va = vaddr & ~(PAGE_SIZE - 1);

	struct page* page = alloc_zeroed_page(AF_NORMAL);
	if (!page) {
		log_error("OOM allocating anon page for vaddr=0x%lx",
			  (unsigned long)vaddr);
		return -ENOMEM;
	}

	int rc = vmm_install_page(vas, mr, va, page);

	// This should drop build ref from alloc and free if vmm_install_page failed
	// Otherwise we just drop the build ref and are good to go
	put_page(page);
	return rc;
}

/**
 * __vmm_populate_one_file - Prepare and map one file-backed page
 * @vas:    target address space
 * @mr:     file-backed region covering @vaddr
 * @vaddr:  virtual address (any offset within the page)
 *
 * Ensures the pagecache page for @vaddr is present and uptodate (reading at
 * most @to_read bytes and zeroing the tail), then installs it via
 * vmm_install_page(). Drops the page’s build ref before returning.
 *
 * Return: 0 on success; -ENOMEM on OOM; -EIO on readpage failure; <0 on
 *         install failure (e.g., -EEXIST).
 *
 * Context: may sleep and perform I/O.
 */
int __vmm_populate_one_file(struct address_space* vas,
			    struct memory_region* mr,
			    vaddr_t vaddr)
{
	/*
	 * File math
	 */
	struct vfs_inode* inode = mr->file.inode;
	struct inode_mapping* map = inode->mapping;

	// Compute file geometry for this faulting page
	size_t page_off = (size_t)(vaddr - mr->start); // offset within VMA
	off_t file_off =
		mr->file.file_lo + (off_t)page_off;    // absolute file offset
	off_t init_left = mr->file.file_hi - file_off; // may be <= 0
	size_t to_read = (size_t)CLAMP(init_left, 0, (off_t)PAGE_SIZE);

	pgoff_t index = (pgoff_t)(file_off >> PAGE_SHIFT);
	size_t tail = PAGE_SIZE - to_read;

	log_debug(
		"FILE: vaddr=0x%lx page_off=0x%zx file_off=0x%llx "
		"file_lo=0x%llx file_hi=0x%llx index=%llu to_read=%zu tail_zero=%zu",
		(unsigned long)vaddr,
		page_off,
		(unsigned long long)file_off,
		(unsigned long long)mr->file.file_lo,
		(unsigned long long)mr->file.file_hi,
		(unsigned long long)index,
		to_read,
		tail);

	/*
	 * This returns a locked page with a build ref.
	 */
	struct page* page = imap_lookup_or_create(map, index);
	if (!page) {
		log_error("OOM creating cache page (index=%llu) for inode=%p",
			  (unsigned long long)index,
			  (void*)inode);
		return -ENOMEM;
	}

	if (to_read == 0) {
		// Entire page is beyond file_hi within the FILE-VMA → pure BSS page
		void* kvaddr = (void*)PHYS_TO_HHDM(page_to_phys(page));
		memset(kvaddr, 0, PAGE_SIZE);
		log_debug("FILE: BSS page zeroed (index=%llu)",
			  (unsigned long long)index);
		page->flags |= PG_UPTODATE;
	} else if (!(page->flags & PG_UPTODATE)) {
		// Cache miss: populate front bytes from disk, then zero the tail
		if (map->imops && map->imops->readpage) {
			int res = map->imops->readpage(inode, page);
			if (res < 0) {
				log_error(
					"Readpage failed (index=%llu, file_off=0x%llx) err=%d",
					(unsigned long long)index,
					(unsigned long long)file_off,
					res);
				put_page(page);
				imap_remove(map, page);
				return -EIO;
			}
			void* kvaddr = (void*)PHYS_TO_HHDM(page_to_phys(page));
			memset((char*)kvaddr + to_read, 0, tail);
			log_debug(
				"FILE: readpage filled %zu bytes, zeroed %zu (index=%llu)",
				to_read,
				tail,
				(unsigned long long)index);
		} else {
			// No readpage -> we must synthesize the page (rare)
			void* kvaddr = (void*)PHYS_TO_HHDM(page_to_phys(page));
			memset(kvaddr, 0, PAGE_SIZE);
			log_warn(
				"FILE: no readpage op; zeroed whole page (index=%llu)",
				(unsigned long long)index);
		}
		page->flags |= PG_UPTODATE;

	} else {
		// Cache hit. Defensively ensure the last page's tail is zero.
		if (to_read < PAGE_SIZE) {
			void* kvaddr = (void*)PHYS_TO_HHDM(page_to_phys(page));
			memset((char*)kvaddr + to_read, 0, tail);
			log_debug(
				"FILE: cache hit; ensured tail-zero %zu bytes (index=%llu)",
				tail,
				(unsigned long long)index);
		} else {
			log_debug(
				"FILE: cache hit; full page content present (index=%llu)",
				(unsigned long long)index);
		}
	}

	// Map into the task's page tables
	vaddr_t aligned_vaddr = vaddr & ~(PAGE_SIZE - 1);
	int rc = vmm_install_page(vas, mr, aligned_vaddr, page);
	if (rc < 0) {
		imap_remove(map, page);
	}

	unlock_page(page);

	/*
 	 * Drop build ref from imap_lookup_or_create; if vmm_install_page
	 * failed, this frees the page.
	 */
	put_page(page);
	return rc;
}

/**
 * vmm_populate_one - Populate a single page according to its VMA policy
 * @vas:    target address space
 * @vaddr:  virtual address (any offset within the page)
 *
 * No-op if already mapped. Otherwise locates the covering VMA and delegates to
 * the appropriate populate helper (anon/file). PTE permissions derive from the
 * VMA; private file mappings are armed for CoW on first write.
 *
 * Return: 0 on success or already present; -EFAULT if no VMA; -EINVAL on bad
 *         args; other negative errors from helpers.
 *
 * Context: may sleep; may perform I/O for file-backed regions.
 */
int vmm_populate_one(struct address_space* vas, vaddr_t vaddr)
{
	if (!vas) return -EINVAL;
	if (!is_within_vas(vas, vaddr)) return -EFAULT;

	vaddr_t va = vaddr & ~(PAGE_SIZE - 1);

	paddr_t p = get_phys_addr(vas->pml4, va);
	if (p) {
		return 0; // Addr exists
	}

	down_read(&vas->vma_lock);
	struct memory_region* mr = get_region(vas, va);
	if (!mr) {
		log_error("No memory region for vaddr 0x%lx", vaddr);
		up_read(&vas->vma_lock);
		return -EFAULT;
	}

	const char* kind = (mr->kind == MR_FILE) ? "FILE" :
			   (mr->kind == MR_ANON) ? "ANON" :
						   "DEVICE";
	char prot_str[4] = { (mr->prot & PROT_READ) ? 'r' : '-',
			     (mr->prot & PROT_WRITE) ? 'w' : '-',
			     (mr->prot & PROT_EXEC) ? 'x' : '-',
			     '\0' };
	log_debug(
		"VMA: [%016lx..%016lx) kind=%s prot=%s flags=0x%lx private=%d",
		(unsigned long)mr->start,
		(unsigned long)mr->end,
		kind,
		prot_str,
		(unsigned long)mr->flags,
		(int)mr->is_private);

	if (mr->kind == MR_ANON) {
		up_read(&vas->vma_lock);
		return __vmm_populate_one_anon(vas, mr, va);
	} else if (mr->kind == MR_FILE) {
		up_read(&vas->vma_lock);
		return __vmm_populate_one_file(vas, mr, va);
	} else {
		log_error("Unknown memory region kind %d", mr->kind);
		up_read(&vas->vma_lock);
		return -EINVAL;
	}

	return 0;
}

/**
 * vmm_write_region - Write data to a virtual memory region
 * @vas: Address space containing the target virtual memory
 * @vaddr: Starting virtual address to write to
 * @data: Source data buffer to copy from
 * @len: Number of bytes to write
 *
 * Writes data to a virtual memory region by translating virtual addresses
 * to physical addresses page by page. Handles writes that span multiple
 * pages by breaking them into page-aligned chunks.
 *
 * Note: This function assumes all target virtual pages are already mapped
 * and accessible. No page fault handling is performed.
 */
void vmm_write_region(struct address_space* vas,
		      vaddr_t vaddr,
		      const void* data,
		      size_t len)
{
	// NOTE: Maybe we should use a memory_region like the name suggests :)
	// Doesn't really change anything though.

	// TODO: Locking

	const u8* data_bytes = data;
	while (len > 0) {
		// Calculate offset within the current page
		size_t page_offset = vaddr & (PAGE_SIZE - 1);

		// Calculate how much we can write in this page
		size_t bytes_in_page = PAGE_SIZE - page_offset;
		size_t bytes_to_copy = (len < bytes_in_page) ? len :
							       bytes_in_page;

		// Translate virtual to physical address
		// TODO: Make sure this returns correct address
		paddr_t paddr = get_phys_addr(vas->pml4, vaddr);

		if (paddr == 0) {
			int rc = vmm_populate_one(vas, vaddr);
			if (rc < 0) {
				log_error(
					"vmm_populate_one failed for vaddr 0x%lx: %d",
					vaddr,
					rc);
				return;
			}
			paddr = get_phys_addr(vas->pml4, vaddr);
			log_debug(
				"Populated page for vaddr 0x%lx, got paddr 0x%lx",
				vaddr,
				paddr);
		}

		vaddr_t kernel_vaddr = PHYS_TO_HHDM(paddr);

		log_debug("Writing %zu bytes to vaddr 0x%lx (phys 0x%lx)",
			  bytes_to_copy,
			  vaddr,
			  paddr);
		if (!data_bytes) {
			memset((char*)kernel_vaddr, 0, bytes_to_copy);
		} else {
			memcpy((char*)kernel_vaddr, data_bytes, bytes_to_copy);
			data_bytes += bytes_to_copy;
		}

		len -= bytes_to_copy;
		vaddr += bytes_to_copy;
	}
}

/*******************************************************************************
* Private Function Definitions
*******************************************************************************/

static bool is_table_empty(pgd_t* table)
{
	// TODO: turn this into a memcmp (ideally architecture specific with rep cmpsb)
	for (size_t i = 0; i < PML4_ENTRIES; i++) {
		if (table[i].pgd != 0) return false; // Found non-empty entry
	}
	return true;
}

/**
 * prune_page_table_recursive - Drop empty page-table nodes under @vaddr
 * @table: Page-table at the current walk level
 * @level: 0=PML4, 1=PDPT, 2=PD, 3=PT (leaf)
 * @vaddr: Virtual address anchoring the walk
 * Return: true if @table is empty after pruning, false otherwise
 * Context: May sleep (frees tables). Locks: none; caller must synchronize
 *          page-table access and TLB shootdowns as needed.
 *
 * Recurses toward the leaf for @vaddr; if a child becomes empty, clears the
 * parent entry and frees the child table. Only prunes non-present subtrees;
 * does not handle huge-page mappings.
 */
static bool
prune_page_table_recursive(uint64_t* table, int level, uintptr_t vaddr)
{
	// TODO: Locking and such
	size_t index = get_table_index(level, vaddr);
	uintptr_t entry = table[index];

	// If the entry is not present, return early
	if ((entry & PAGE_PRESENT) == 0) {
		return is_table_empty((pgd_t*)table);
	}

	// If we are not at the leaf, we need to recurse
	if (level < 3) {
		uint64_t* child_table =
			(uint64_t*)PHYS_TO_HHDM(entry & PAGE_FRAME_MASK);
		if (prune_page_table_recursive(child_table, level + 1, vaddr)) {
			table[index] =
				0; // Clear the entry if child table was pruned
			_free_page_table(child_table);
			log_debug("Freed PT at level %d (vaddr: 0x%lx)",
				  level,
				  vaddr);
		}
	}

	return is_table_empty((pgd_t*)table);
}

/**
 * walk_page_table - Return leaf PTE for @vaddr, optionally creating tables
 * @pml4:  Top-level page table
 * @vaddr: Virtual address to walk (must be canonical)
 * @create: Allocate intermediate tables if missing
 * @flags:  Flags to apply to newly created non-leaf entries (include PRESENT)
 * Return: Pointer to leaf PTE, or NULL if absent and @create==false
 * Context: May sleep if @create and allocator sleeps. No locks taken; caller
 *          must hold page-table lock and manage IRQ state. Huge pages not set.
 */
static pte_t*
walk_page_table(pgd_t* pml4, uptr vaddr, bool create, flags_t flags)
{
	if (create && (flags & PAGE_PRESENT) == 0) {
		log_warn(
			"walk_page_table creating an entry WITHOUT PAGE_PRESENT! flags: 0x%lx",
			flags);
	}
	// Ensure the virtual address is canonical
	if ((vaddr >> 48) != 0 && (vaddr >> 48) != 0xFFFF) return nullptr;

	// Mask the flags to ensure only valid bits are used
	flags &= FLAGS_MASK;

	// Get the PML4 index for the virtual address
	uint64_t pml4_i = _pml4_index(vaddr);
	if ((pml4[pml4_i].pgd & PAGE_PRESENT) == 0) {
		if (!create) return NULL;
		// NOTE: We are casting to a u64 here because that is what the HHDM_TO_PHYS macro expects
		pml4[pml4_i].pgd =
			(u64)HHDM_TO_PHYS(_alloc_page_table(AF_KERNEL)) | flags;
	}

	// Get the PDPT from the PML4 entry
	pud_t* pdpt = (pud_t*)PHYS_TO_HHDM(pml4[pml4_i].pgd &
					   ~FLAGS_MASK); // Mask off flags
	uint64_t pdpt_i = _pdpt_index(vaddr);
	if ((pdpt[pdpt_i].pud & PAGE_PRESENT) == 0) {
		if (!create) return NULL;
		pdpt[pdpt_i].pud =
			(u64)HHDM_TO_PHYS(_alloc_page_table(AF_KERNEL)) | flags;
	}

	// Get the PD from the PDPT entry
	pmd_t* pd = (pmd_t*)PHYS_TO_HHDM(pdpt[pdpt_i].pud &
					 ~FLAGS_MASK); // Mask off flags
	uint64_t pd_i = _pd_index(vaddr);
	if ((pd[pd_i].pmd & PAGE_PRESENT) == 0) {
		if (!create) return NULL;
		pd[pd_i].pmd = (u64)HHDM_TO_PHYS(_alloc_page_table(AF_KERNEL)) |
			       flags;
	}

	// Get the PT from the PD entry
	pte_t* pt = (pte_t*)PHYS_TO_HHDM(pd[pd_i].pmd & ~FLAGS_MASK);
	uint64_t pt_i = _pt_index(vaddr);

	// Return the pointer to the page table entry
	return pt + pt_i;
}

/**
 * map_memmap_entry - Map a bootloader memmap span into kernel space
 * @pml4:          Top-level page table
 * @entry:         Boot info memory-map entry to mirror
 * @k_vstart:     Kernel virtual start
 * @k_pstart:     Kernel physical start
 * @k_size:       Kernel size
 * Context: Early boot mapping helper; assumes single-CPU init. No locks.
 * Notes: Maps the span into HHDM; for EXECUTABLE/MODULES also maps an
 *        executable alias at @exe_virt_base + offset.
 */
static void map_memmap_entry(pgd_t* pml4,
			     struct bootinfo_memmap_entry* entry,
			     uptr k_vstart,
			     uptr k_pstart,
			     size_t k_size)
{
	flags_t flags;
	switch (entry->type) {
	case LIMINE_MEMMAP_USABLE:
	case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
	case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
		flags = PAGE_PRESENT | PAGE_WRITE | CACHE_WRITE_BACK |
			PAGE_NO_EXECUTE;
		break;
	case LIMINE_MEMMAP_FRAMEBUFFER:
		flags = PAGE_PRESENT | PAGE_WRITE | CACHE_WRITE_COMBINING |
			PAGE_NO_EXECUTE;
		break;
	default: return;
	}

	uintptr_t start = entry->base;
	uintptr_t end = entry->base + entry->length;
	log_debug("Mapping [%lx-%lx), type: %lu", start, end, entry->type);
	for (size_t phys = start; phys < end; phys += PAGE_SIZE) {
		vmm_map_frame_alias(pml4, PHYS_TO_HHDM(phys), phys, flags);
	}

	// Skip exe alias if entry is not an executable
	if (entry->type != LIMINE_MEMMAP_EXECUTABLE_AND_MODULES) {
		return;
	}

	// Now we map the executable alias
	uptr phys_lo = MAX(start, k_pstart);
	uptr phys_hi = MIN(end, k_pstart + k_size);
	if (phys_lo >= phys_hi) {
		return;
	}

	for (uptr phys = phys_lo; phys < phys_hi; phys += PAGE_SIZE) {
		uptr v = k_vstart + (phys - k_pstart);
		vmm_map_frame_alias(pml4, v, phys, flags & ~PAGE_NO_EXECUTE);
	}
}

/**
 * log_page_table_walk - Dump PML4→PT entries for @vaddr
 * @pml4:  Top-level page table (virtual, via HHDM)
 * @vaddr: Address to trace
 * Context: Debug-only; read-only walk; IRQ-safe; no locks. Races tolerated.
 * Notes: Prints each level and notes huge-page stops or not-present entries.
 */
static void log_page_table_walk(u64* pml4, vaddr_t vaddr)
{
	size_t pml4_i = _pml4_index(vaddr);
	size_t pdpt_i = _pdpt_index(vaddr);
	size_t pd_i = _pd_index(vaddr);
	size_t pt_i = _pt_index(vaddr);

	uint64_t pml4e = pml4[pml4_i];
	log_info("PML4E [%03lx] = 0x%016lx", pml4_i, pml4e);

	if (!(pml4e & PAGE_PRESENT)) {
		log_warn("  PML4E not present!");
		return;
	}

	uint64_t* pdpt = (uint64_t*)PHYS_TO_HHDM(pml4e & PAGE_FRAME_MASK);
	uint64_t pdpte = pdpt[pdpt_i];
	log_info(" PDPT [%03lx] = 0x%016lx", pdpt_i, pdpte);

	if (!(pdpte & PAGE_PRESENT)) {
		log_warn("  PDPT entry not present!");
		return;
	}

	uint64_t* pd = (uint64_t*)PHYS_TO_HHDM(pdpte & PAGE_FRAME_MASK);
	uint64_t pde = pd[pd_i];
	log_info("  PD  [%03lx] = 0x%016lx", pd_i, pde);

	if (!(pde & PAGE_PRESENT)) {
		log_warn("  PD entry not present!");
		return;
	}
	if (pde & PDE_PS) {
		log_info("  PD entry is a huge (2MiB) page.");
		return;
	}

	uint64_t* pt = (uint64_t*)PHYS_TO_HHDM(pde & PAGE_FRAME_MASK);
	uint64_t pte = pt[pt_i];
	log_info("   PT  [%03lx] = 0x%016lx", pt_i, pte);

	if (!(pte & PAGE_PRESENT)) {
		log_warn("  PT entry not present!");
		return;
	}
}

/**
 * do_demand_paging - Handle a not-present page fault for current task
 * @r: Fault frame registers
 * Return: 0 on success or -errno from population
 * Context: Page-fault path; must not sleep beyond what the handler allows.
 * Notes: Derives access type from PF errcode, checks VMA permissions, and
 *        populates a single page via vmm_populate_one().
 */
static int do_demand_paging(struct registers* r)
{
	struct task* task = get_current_task();
	struct address_space* vas = task->vas;

	uint64_t fault_addr;
	__asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));

	vaddr_t vaddr = align_down_page(fault_addr);

	bool need_exec = r->err_code & 0x10;
	bool need_write = r->err_code & 0x2;
	bool need_read = !need_write;

	check_access(vas, vaddr, need_read, need_write, need_exec);

	return vmm_populate_one(vas, vaddr);
}

/**
 * page_fault - x86-64 page-fault top-half
 * @r: Fault frame registers
 * Context: Fault handler; IRQ state per entry; reentrancy not expected.
 * Notes: Routes not-present faults to demand paging, handles CoW on write
 *        faults, and calls page_fault_fail() on irrecoverable errors.
 */
static void page_fault(struct registers* r)
{
	if (!is_scheduler_init()) {
		page_fault_fail(r);
	}

	struct task* task = get_current_task();
	struct address_space* vas = task->vas;

	uint64_t fault_addr;
	__asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));
	uint64_t cr3;
	__asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

	// Decode PF error code bits (x86-64)
	bool pf_present = r->err_code & 0x1; // 0 = not-present, 1 = protection
	bool pf_write = r->err_code & 0x2;
	bool pf_user = r->err_code & 0x4;
	bool pf_rsvd = r->err_code & 0x8;
	bool pf_exec = r->err_code & 0x10; // instruction fetch (NX)

	log_debug(
		"PF: cr2=0x%lx rip=0x%lx ec=0x%lx [P=%d W=%d U=%d I=%d RSVD=%d] "
		"vas.PML4=0x%lx pid=%d",
		(unsigned long)fault_addr,
		(unsigned long)r->rip,
		(unsigned long)r->err_code,
		pf_present,
		pf_write,
		pf_user,
		pf_exec,
		pf_rsvd,
		(unsigned long)vas->pml4_phys,
		task->pid);

	bool is_write_fault = r->err_code & 0x2;
	bool is_present_fault = r->err_code & 0x1;

	if (!is_present_fault) {
		// This is not a CoW fault.
		int dc = do_demand_paging(r);
		if (dc == 0) {
			return;
		}
		log_error("Demand paging failed with err=%d", dc);
		page_fault_fail(r); // TODO: SEGV
	}
	if (!is_write_fault) {
		page_fault_fail(r);
	}

	vaddr_t page_aligned_addr = fault_addr & PAGE_FRAME_MASK;

	if (vas->pml4_phys != cr3) {
		page_fault_fail(r);
	}

	struct memory_region* mr = get_region(vas, page_aligned_addr);
	if (!mr || !(mr->prot & PROT_WRITE)) {
		page_fault_fail(r); // TODO: SEGV
	}

	log_debug("Faulted in address_space %lx", cr3);
	// address_space_dump(vas);

	pte_t* pte = walk_page_table(vas->pml4, page_aligned_addr, false, 0);
	if (!pte) {
		page_fault_fail(r);
	}

	paddr_t shared_paddr = pte->pte & PAGE_FRAME_MASK;
	struct page* shared_page = phys_to_page(shared_paddr);

	bool want_cow = false;

	switch (mr->kind) {
	case MR_FILE:
		// Private file mappings must NEVER dirty the file: always CoW.
		want_cow = mr->is_private;
		break;

	case MR_ANON:
		if (mr->is_private) {
			// Fork-style CoW only when physically shared
			// TODO: Check for zero page
			bool phys_shared = atomic_read(&shared_page->mapcount) >
					   1;
			want_cow = phys_shared;
		} else {
			// Shared anon/shmem: write-through, no CoW.
			want_cow = false;
		}
		break;

	default: // MR_DEVICE etc.
		// Typically deny or special-case; don’t CoW MMIO.
		page_fault_fail(r); // TODO: SEGV
	}

	if (want_cow) {
		struct page* new_page = alloc_page(AF_NORMAL);
		if (!new_page) {
			log_error("OOM during CoW fault!");
			page_fault_fail(r);
		}

		// If CoW came from a private FILE mapping, the new page is anonymous:
		if (mr->kind == MR_FILE) {
			new_page->mapping = nullptr;
			new_page->flags &= ~PG_MAPPED;
		}

		paddr_t new_paddr = page_to_phys(new_page);

		// Actually do the copy part
		void* dest_kvaddr = (void*)PHYS_TO_HHDM(new_paddr);
		void* src_kvaddr = (void*)PHYS_TO_HHDM(shared_paddr);
		memcpy(dest_kvaddr, src_kvaddr, PAGE_SIZE);

		// Update mappings
		flags_t flags = (pte->pte & FLAGS_MASK) | PAGE_WRITE;
		vmm_unmap_page(vas->pml4, page_aligned_addr);
		vmm_map_page(vas->pml4, page_aligned_addr, new_paddr, flags);

		put_page(new_page); // Drop build ref from alloc_page()
	} else {
		flags_t new_flags = (pte->pte & FLAGS_MASK) | PAGE_WRITE;
		vmm_protect_page(vas, page_aligned_addr, new_flags);
		shared_page->flags |= PG_DIRTY;
	}
}

/**
 * page_fault_fail - Fatal page fault handler (no return)
 * @r: Fault frame registers
 * Context: Fault handler; logs synchronously and panics.
 * Notes: Dumps task, registers, and a page-table walk before halting.
 */
[[noreturn]]
static void page_fault_fail(struct registers* r)
{
	// GDB BREAKPOINT
	uint64_t fault_addr;
	__asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));
	uint64_t cr3;
	__asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

	int present = (int)(!(r->err_code & 0x1));
	int rw = (int)(r->err_code & 0x2);
	int user = (int)(r->err_code & 0x4);
	int reserved = (int)(r->err_code & 0x8);
	int id = (int)(r->err_code & 0x10);

	set_log_mode(LOG_DIRECT);
	irq_log_flush();
	console_flush();
	klog_flush();

	log_error("=== PAGE FAULT ===");

	struct task* task = get_current_task();
	log_error("Faulting task: '%s' (PID: %d)", task->name, task->pid);

	VAS_DUMP(task->vas);

	// void* return_address = (void*)(*(u64*)(r->rbp + 8));
	//
	// log_error("Return address: %p", return_address);

	log_error(
		"PAGE FAULT! err %lu (p:%d,rw:%d,user:%d,res:%d,id:%d) at 0x%lx. Caused by 0x%lx in address space %lx",
		r->err_code,
		present,
		rw,
		user,
		reserved,
		id,
		fault_addr,
		r->rip,
		cr3);

	if (!present) {
		log_error("Reason: The page was not present in memory.");
	}
	if (rw) {
		log_error(
			"Violation: This was a write operation to a read-only page.");
	} else {
		log_error("Violation: This was a read operation.");
	}
	if (user) {
		log_error("Context: The fault occurred in user-mode.");
	} else {
		log_error("Context: The fault occurred in kernel-mode.");
	}
	if (reserved) {
		log_error(
			"Details: A reserved bit was set in a page directory entry.");
	}
	if (id) {
		log_error(
			"Details: The fault was caused by an instruction fetch.");
	}

	log_error("General registers:");
	log_error("RIP: %lx, RSP: %lx, RBP: %lx", r->rip, r->rsp, r->rbp);
	log_error("RAX: %lx, RBX: %lx, RCX: %lx, RDX: %lx",
		  r->rax,
		  r->rbx,
		  r->rcx,
		  r->rdx);
	log_error("RDI: %lx, RSI: %lx, RFLAGS: %lx, DS: %lx",
		  r->rdi,
		  r->rsi,
		  r->rflags,
		  r->ds);
	log_error("CS: %lx, SS: %lx", r->cs, r->ss);
	log_error("R8: %lx, R9: %lx, R10: %lx, R11: %lx",
		  r->r8,
		  r->r9,
		  r->r10,
		  r->r11);
	log_error("R12: %lx, R13: %lx, R14: %lx, R15: %lx",
		  r->r12,
		  r->r13,
		  r->r14,
		  r->r15);

	log_page_table_walk((uint64_t*)PHYS_TO_HHDM(cr3), fault_addr);

	// This calls console_flush()
	panic("Page Fault");
}
