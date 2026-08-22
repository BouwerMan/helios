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

/*
 * Unlike my original vmm implementation, this one only focuses on paging and address space magaement.
 * Overview:
 * 	1. Kernel inits bootmem
 * 	2. Kernel inits page_alloc
 * 	3. Kernel decommissions bootmem which then releases limine reclaimable resources
 * 	4. We init our kernel address space
 *
 * We will have a mapping of the entire physical memory space at hhdm_offset.
 */

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
#include "kernel/klog.h"
#include "kernel/panic.h"
#include "kernel/spinlock.h"
#include "kernel/tasks/scheduler.h"
#include "lib/string.h"
#include "mm/address_space.h"
#include "mm/address_space_dump.h"
#include "mm/kmalloc.h"
#include "mm/page.h"
#include "mm/page_alloc.h"

#include <stddef.h>
#include <stdint.h>
#include <uapi/helios/errno.h>
#include <uapi/helios/mman.h>

extern char __kernel_start[], __kernel_end[];

/*******************************************************************************
 * Private Function Prototypes
 *******************************************************************************/

static void page_fault(struct registers* r);

[[noreturn]] static void page_fault_fail(struct registers* r);

static int do_demand_paging(struct registers* r);

static pte_t* walk_page_table(pgd_t* pml4, vaddr_t vaddr, bool create, flags_t flags);

static void
map_memmap_entry(pgd_t* pml4, struct bootinfo_memmap_entry* entry, uptr k_vstart, uptr k_pstart, size_t k_size);

static void log_page_table_walk(u64* pml4, vaddr_t vaddr);

static bool is_table_empty(pgd_t* table);

static bool prune_page_table_recursive(uint64_t* table, int level, uintptr_t vaddr);

/*******************************************************************************
 * Private inline helpers
 ******************************************************************************/

/**
 * @brief Invalidates a single TLB entry with invlpg.
 *
 * @param vaddr Virtual address whose translation to invalidate.
 *
 * @note Local CPU only. Does not broadcast. Callers handle shootdowns.
 */
static inline void invalidate(vaddr_t vaddr)
{
	__asm__ volatile("invlpg (%0)" ::"r"(vaddr) : "memory");
}

/**
 * @brief Extracts the 9-bit index for a page-table level.
 *
 * @param vaddr Virtual address.
 * @param shift Bit position of the level's index (39, 30, 21, or 12).
 *
 * @return Index in the range [0, 511].
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
 * @brief Extracts the 9-bit page-table index at a given level.
 *
 * @param level Walk level. 0 is PML4, 1 is PDPT, 2 is PD, 3 is PT.
 * @param vaddr Virtual address to decode.
 *
 * @return Index in the range [0, 511].
 *
 * @note Pure. Does not sleep. IRQ-safe. Takes no locks.
 *
 * The caller must ensure @p level is valid for the current paging mode.
 */
static inline size_t get_table_index(int level, uintptr_t vaddr)
{
	/* Levels: 0=PML4, 1=PDPT, 2=PD, 3=PT */
	kassert((unsigned)level <= 3, "bad pt level");
	return (vaddr >> (39 - 9 * level)) & 0x1FF;
}

/**
 * @brief Allocates a single 4 KiB page-table frame.
 *
 * @param flags Allocation flags for the low-level page allocator.
 *
 * @return Zeroed memory sized for one page-table.
 *
 * @note x86 paging rules require new page tables to be zeroed. If the
 * allocator does not guarantee zeroed pages, the caller must clear it.
 */
static inline void* _alloc_page_table(aflags_t flags)
{
	return (void*)get_free_pages(flags, PML4_SIZE_PAGES);
}

/**
 * @brief Frees a single 4 KiB page-table frame.
 *
 * @param table Pointer previously returned by _alloc_page_table().
 */
static inline void _free_page_table(void* table)
{
	free_pages(table, PML4_SIZE_PAGES);
}

/**
 * @brief Translates region protection to x86 PTE flags.
 *
 * @param mr Memory region descriptor.
 *
 * @return Initial PTE flags for leaf mappings.
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
 * @brief Initializes paging and the kernel address space.
 *
 * @note Runs at early boot on the BSP. Non-preemptible.
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
		map_memmap_entry((pgd_t*)kernel.pml4, entry, k_vstart, k_pstart, kernel_size);
	}

	vmm_load_cr3(HHDM_TO_PHYS(kernel.pml4));
}

/**
 * @brief Allocates a fresh top-level page table.
 *
 * @return Pointer to a new PML4 initialized from the kernel template.
 *
 * @note May sleep depending on the allocator. Panics on out-of-memory.
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
 * @brief Installs a PRESENT PTE and takes the mapping pin.
 *
 * @param pml4 Page-table root.
 * @param vaddr Page-aligned virtual address. Must be unmapped.
 * @param paddr Page-aligned physical address to map.
 * @param flags PTE flags (USER, WRITE, PRESENT, NX, and so on).
 *
 * @return 0 on success. -EINVAL on misalignment. -EFAULT if a PTE is
 * already present or the walk failed. Other -errno values as implemented.
 *
 * Fails if a PRESENT PTE already exists for @p vaddr. Takes exactly one
 * mapping reference (get_page) on the mapped frame on success. Must not
 * sleep. The caller handles higher-level policy and locking.
 */
int vmm_map_page(pgd_t* pml4, uintptr_t vaddr, uintptr_t paddr, flags_t flags)
{
	if (!is_page_aligned(vaddr) || !is_page_aligned(paddr)) {
		log_error("Something isn't aligned right, vaddr: %lx, paddr: %lx", vaddr, paddr);
		return -EINVAL;
	}

	// We want PAGE_PRESENT and PAGE_WRITE on almost all the higher levels
	flags_t walk_flags = flags & (PAGE_USER | PAGE_PRESENT | PAGE_WRITE);
	pte_t* pte = walk_page_table(pml4, vaddr, true, walk_flags | PAGE_PRESENT | PAGE_WRITE);

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
 * @brief Maps a physical address at a virtual address without owning a
 * page reference.
 *
 * @param pml4 Page-table root.
 * @param vaddr Page-aligned virtual address.
 * @param paddr Page-aligned physical address.
 * @param flags PTE flags.
 *
 * @return 0 on success. -EINVAL on misalignment. -EFAULT if already mapped.
 *
 * Creates a non-owning alias mapping, with no get_page or mapcount
 * changes. Intended for the HHDM, identity maps, and MMIO.
 */
int vmm_map_frame_alias(pgd_t* pml4, uintptr_t vaddr, uintptr_t paddr, flags_t flags)
{
	if (!is_page_aligned(vaddr) || !is_page_aligned(paddr)) {
		log_error("Something isn't aligned right, vaddr: %lx, paddr: %lx", vaddr, paddr);
		return -EINVAL;
	}

	// We want PAGE_PRESENT and PAGE_WRITE on almost all the higher levels
	flags_t walk_flags = flags & (PAGE_USER | PAGE_PRESENT | PAGE_WRITE);
	pte_t* pte = walk_page_table(pml4, vaddr, true, walk_flags | PAGE_PRESENT | PAGE_WRITE);

	if (!pte) {
		log_warn("Could not find pte, vaddr: %lx, paddr: %lx", vaddr, paddr);
		return -EFAULT;
	}
	if (pte->pte & PAGE_PRESENT) {
		log_warn("PTE already present, vaddr: %lx, paddr: %lx, pte: %lx", vaddr, paddr, pte->pte);
		return -EFAULT;
	}

	pte->pte = paddr | flags;

	return 0;
}

/**
 * @brief Removes a PRESENT PTE and drops the mapping pin.
 *
 * @param pml4 Page-table root.
 * @param vaddr Page-aligned virtual address.
 *
 * @return 0 on success, including when already unmapped. -EINVAL on
 * misalignment.
 *
 * Idempotently clears a PRESENT PTE at @p vaddr, if any. Drops exactly one
 * mapping reference (put_page) on the mapped frame. Prunes now-empty
 * page-table levels and invalidates the TLB for @p vaddr.
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
 * @brief Frees empty page-table nodes under an address.
 *
 * @param pml4 Top-level page table to prune.
 * @param vaddr Virtual address whose walk anchors the prune.
 *
 * @return 0.
 *
 * @note May sleep. IRQs must stay enabled. The caller must synchronize
 * page-table access.
 *
 * Recursively drops intermediate page-table levels that hold no present
 * entries along the walk rooted at @p vaddr. Does not change leaf mappings
 * and does not perform TLB shootdowns. Callers handle any invalidation.
 */
int prune_page_tables(uint64_t* pml4, uintptr_t vaddr)
{
	(void)prune_page_table_recursive(pml4, 0, vaddr);

	return 0;
}

/**
 * @brief Resolves a virtual address to a physical address.
 *
 * @param pml4 Top-level page table used for the walk.
 * @param vaddr Virtual address to translate.
 *
 * @return The physical address on success, or 0 if unmapped or not present.
 *
 * @note Does not sleep. IRQ-safe. Takes no locks and makes no TLB changes.
 *
 * Performs a non-allocating walk to find the leaf PTE for @p vaddr. This
 * function does not perform user or supervisor access checks.
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
 * @brief Maps a region by allocating fresh pages.
 *
 * @param vas Target address space.
 * @param mr Region with a [start, end) range and protections.
 *
 * @return 0 or -errno.
 *
 * @note May sleep. Acquires @p vas->vma_lock for read and @p vas->pgt_lock.
 *
 * Maps one zeroed page per PTE using flags from @p mr->prot. On failure,
 * unmaps pages that this call created.
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
		irqf = spin_lock_irqsave(&vas->pgt_lock);

		// Mapped by someone else?
		if (get_phys_addr(vas->pml4, v)) {
			spin_unlock_irqrestore(&vas->pgt_lock, irqf);
			up_read(&vas->vma_lock);
			put_page(page); // Drop build ref
			continue;
		}

		err = vmm_map_page(vas->pml4, v, paddr,
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
		spinflags = spin_lock_irqsave(&vas->pgt_lock);
		(void)vmm_unmap_page(vas->pml4, u);
		spin_unlock_irqrestore(&vas->pgt_lock, spinflags);
	}
	return err;
}

/**
 * @brief Maps a region for MMIO.
 *
 * @param vas Target address space.
 * @param mr Region with a [start, end) range and protections.
 *
 * @return 0 or -errno.
 *
 * @note May sleep. Acquires @p vas->vma_lock for read and @p vas->pgt_lock.
 */
int vmm_map_device_region(struct address_space* vas, struct memory_region* mr)
{
	if (!vas || !mr) {
		return -EINVAL;
	}

	kassert(mr->kind == MR_DEVICE);

	int err = 0;

	vaddr_t v = mr->start;
	paddr_t p = mr->dev.paddr;
	for (; v < mr->end; v += PAGE_SIZE, p += PAGE_SIZE) {
		down_read(&vas->vma_lock);
		// Make sure things aren't shifting around on us
		if (!is_within_region(mr, v)) {
			up_read(&vas->vma_lock);
			log_error("virtual address moved outside of memory region");
			err = -EFAULT;
			goto clean;
		}

		flags_t flags = flags_from_mr(mr) | CACHE_WRITE_COMBINING;
		kassert(flags & PAGE_NO_EXECUTE);

		unsigned long irqf;
		irqf = spin_lock_irqsave(&vas->pgt_lock);

		vmm_map_frame_alias(vas->pml4, v, p, flags);

		spin_unlock_irqrestore(&vas->pgt_lock, irqf);
		up_read(&vas->vma_lock);
	}

	return 0;

clean:
	for (vaddr_t u = mr->start; u < v; u += PAGE_SIZE) {
		unsigned long spinflags;
		spinflags = spin_lock_irqsave(&vas->pgt_lock);
		pte_t* pte = walk_page_table(vas->pml4, u, false, 0);

		if (pte && pte->pte & PAGE_PRESENT) {
			pte->pte = 0;
			invalidate(u);
		}
		spin_unlock_irqrestore(&vas->pgt_lock, spinflags);
	}
	return err;
}

/**
 * @brief Mirrors a region into a destination address space, with
 * copy-on-write for private regions.
 *
 * @param dest_vas Destination address space (child).
 * @param src_mr Source region in its owner address space (parent).
 *
 * @return 0 or -errno. -ENOTSUP for devices. -ENOMEM on allocation failure.
 *
 * @note May sleep. The caller must hold @p src_mr->owner->vma_lock for
 * read. This call does not hold @p dest_vas->vma_lock, which is safe only
 * because @p dest_vas is not yet visible to any other task.
 *
 * Maps present pages into @p dest_vas. For private regions, clears
 * PAGE_WRITE in both parent and child to arm copy-on-write. Skips
 * non-present pages; demand paging handles them later.
 */
int vmm_fork_region(struct address_space* dest_vas, struct memory_region* src_mr)
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

	size_t num_pages = (src_mr->end - src_mr->start) >> PAGE_SHIFT;
	// temporary guard: 4GB limit @ 4K pages
	if (num_pages > (1UL << 20)) {
		return -ENOMEM;
	}

	size_t prot_idx = 0;
	bool* protected = kzalloc(num_pages);
	if (!protected) {
		return -ENOMEM;
	}

	memset(protected, 0, num_pages);

	for (v = src_mr->start; v < src_mr->end; v += PAGE_SIZE, prot_idx++) {
		unsigned long irqf;
		irqf = spin_lock_irqsave(&src_vas->pgt_lock);
		pte_t* src_pte = walk_page_table(src_vas->pml4, v, false, 0);
		u64 snapshot = src_pte ? src_pte->pte : 0;
		spin_unlock_irqrestore(&src_vas->pgt_lock, irqf);

		if (!(snapshot & PAGE_PRESENT)) continue; // demand-paged later

		bool priv = src_mr->is_private;
		paddr_t p = snapshot & X86_PTE_ADDR_MASK;
		flags_t current_flags = snapshot & (X86_PTE_LOWFLAGS | X86_PTE_NX);
		flags_t new_flags = priv ? (current_flags & ~PAGE_WRITE) : current_flags;

		/* Map into child */
		irqf = spin_lock_irqsave(&src_vas->pgt_lock);
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
	return 0;

clean:
	log_error("Failed to fork region: %d", out_err);

	vaddr_t cleanup_end = v; // Don't include the failed page
	prot_idx = 0;
	for (v = src_mr->start; v < cleanup_end; v += PAGE_SIZE, prot_idx++) {
		unsigned long irqf;
		irqf = spin_lock_irqsave(&src_vas->pgt_lock);

		// Find the original flags to restore them
		pte_t* src_pte = walk_page_table(src_vas->pml4, v, false, 0);
		u64 snapshot = src_pte ? src_pte->pte : 0;
		spin_unlock_irqrestore(&src_vas->pgt_lock, irqf);

		if (snapshot & PAGE_PRESENT) {
			flags_t original_flags = (snapshot & FLAGS_MASK) | PAGE_WRITE;
			// Restore parent write permissions if we removed them
			if (protected[prot_idx]) {
				vmm_protect_page(src_vas, v, original_flags);
			}
		}

		irqf = spin_lock_irqsave(&src_vas->pgt_lock);
		int res = vmm_unmap_page(dest_vas->pml4, v);
		spin_unlock_irqrestore(&src_vas->pgt_lock, irqf);

		if (res < 0) {
			panic("Could not cleanup vmm_fork_region");
		}
	}

	kfree(protected);
	return err;
}

/**
 * @brief Removes all mappings within a region.
 *
 * @param vas Address space that owns the mappings.
 * @param mr Region with a [start, end) range to unmap.
 *
 * @return 0 or -errno from vmm_unmap_page().
 *
 * @note May sleep. Acquires @p vas->vma_lock for read and @p vas->pgt_lock.
 *
 * Drops PTEs. Underlying page freeing follows separate refcount policy.
 */
int vmm_unmap_region(struct address_space* vas, struct memory_region* mr)
{
	down_read(&vas->vma_lock);

	for (vaddr_t v = mr->start; v < mr->end; v += PAGE_SIZE) {
		unsigned long spinflags;
		spinflags = spin_lock_irqsave(&vas->pgt_lock);

		// If it is a device we just need to clear the PTE, otherwise
		// we have to do all the funny ref count managing that
		// vmm_unmap_page does
		if (mr->kind == MR_DEVICE) {
			pte_t* pte = walk_page_table(vas->pml4, v, false, 0);
			if (pte && pte->pte & PAGE_PRESENT) {
				pte->pte = 0;
				invalidate(v);
			}
		} else {
			int err = vmm_unmap_page(vas->pml4, v);
			if (err < 0) {
				spin_unlock_irqrestore(&vas->pgt_lock, spinflags);
				up_read(&vas->vma_lock);
				return err;
			}
		}
		spin_unlock_irqrestore(&vas->pgt_lock, spinflags);
	}

	up_read(&vas->vma_lock);
	return 0;
}

/**
 * @brief Replaces the PTE permission bits for one page.
 *
 * @param vas Address space.
 * @param vaddr Page-aligned virtual address.
 * @param new_prot New flags. Include PRESENT and USER as appropriate.
 *
 * @return 0 or -errno. -EINVAL for a bad @p vas. -EFAULT if unmapped or
 * not present.
 *
 * @note Does not sleep. Takes @p vas->pgt_lock, with IRQs disabled inside.
 *
 * Preserves the frame address. Updates only the flags and invalidates the
 * local TLB.
 */
int vmm_protect_page(struct address_space* vas, vaddr_t vaddr, flags_t new_prot)
{
	if (!vas) return -EINVAL;

	unsigned long spinflags;
	spinflags = spin_lock_irqsave(&vas->pgt_lock);

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
 * @brief Finalizes the mapping of a prepared page into a VMA.
 *
 * @param vas Target address space that owns @p mr.
 * @param mr Covering memory_region in @p vas.
 * @param vaddr Page-aligned virtual address within @p mr.
 * @param page Page with a build ref (ref > 0). Content already prepared.
 *
 * @return 0 on success or on an identical existing mapping. -EEXIST if
 * mapped to a different frame. -ENOTSUP for MR_DEVICE. -EFAULT if @p vaddr
 * is not in @p mr. -EINVAL on bad arguments. Other errors come from
 * vmm_map_page().
 *
 * Acquires @p vas->vma_lock for read and @p vas->pgt_lock, then installs a
 * PRESENT PTE for @p vaddr. For MR_FILE with MAP_PRIVATE, clears WRITE to
 * arm copy-on-write.
 *
 * @note Does not touch the caller's build ref. vmm_map_page() takes the
 * mapping pin on success. The caller should call put_page(@p page) after.
 */
int vmm_install_page(struct address_space* vas, struct memory_region* mr, vaddr_t vaddr, struct page* page)
{
	if (!vas || !mr || !page || mr->owner != vas || !is_page_aligned(vaddr)) {
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
	spinflags = spin_lock_irqsave(&vas->pgt_lock);

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
 * @brief Prepares and maps one anonymous page.
 *
 * @param vas Target address space.
 * @param mr Anonymous memory region that covers @p vaddr.
 * @param vaddr Virtual address, at any offset within the page.
 *
 * @return 0 on success. -ENOMEM on out-of-memory. Negative on install
 * failure.
 *
 * @note May sleep. Performs no I/O.
 *
 * Allocates a zeroed page with a build ref, then calls vmm_install_page()
 * to map it. Always drops the build ref before returning.
 */
int __vmm_populate_one_anon(struct address_space* vas, struct memory_region* mr, vaddr_t vaddr)
{
	if (!vas || !mr) return -EINVAL;

	vaddr_t va = vaddr & ~(PAGE_SIZE - 1);

	struct page* page = alloc_zeroed_page(AF_NORMAL);
	if (!page) {
		log_error("OOM allocating anon page for vaddr=0x%lx", (unsigned long)vaddr);
		return -ENOMEM;
	}

	int rc = vmm_install_page(vas, mr, va, page);

	// This should drop build ref from alloc and free if vmm_install_page failed
	// Otherwise we just drop the build ref and are good to go
	put_page(page);
	return rc;
}

/**
 * @brief Prepares and maps one file-backed page.
 *
 * @param vas Target address space.
 * @param mr File-backed region that covers @p vaddr.
 * @param vaddr Virtual address, at any offset within the page.
 *
 * @return 0 on success. -ENOMEM on out-of-memory. -EIO on readpage
 * failure. Negative on install failure, for example -EEXIST.
 *
 * @note May sleep and perform I/O.
 *
 * Ensures the pagecache page for @p vaddr is present and up to date, then
 * installs it with vmm_install_page(). Drops the page's build ref before
 * returning.
 */
int __vmm_populate_one_file(struct address_space* vas, struct memory_region* mr, vaddr_t vaddr)
{
	/*
	 * File math
	 */
	struct vfs_inode* inode = mr->file.inode;
	struct inode_mapping* map = inode->mapping;

	// Compute file geometry for this faulting page
	size_t page_off = (size_t)(vaddr - mr->start);	     // offset within VMA
	off_t file_off = mr->file.file_lo + (off_t)page_off; // absolute file offset
	off_t init_left = mr->file.file_hi - file_off;	     // may be <= 0
	size_t to_read = (size_t)CLAMP(init_left, 0, (off_t)PAGE_SIZE);

	pgoff_t index = (pgoff_t)(file_off >> PAGE_SHIFT);
	size_t tail = PAGE_SIZE - to_read;

	log_debug("FILE: vaddr=0x%lx page_off=0x%zx file_off=0x%llx "
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
		log_error("OOM creating cache page (index=%llu) for inode=%p", (unsigned long long)index, (void*)inode);
		return -ENOMEM;
	}

	if (to_read == 0) {
		// Entire page is beyond file_hi within the FILE-VMA → pure BSS page
		void* kvaddr = (void*)PHYS_TO_HHDM(page_to_phys(page));
		memset(kvaddr, 0, PAGE_SIZE);
		log_debug("FILE: BSS page zeroed (index=%llu)", (unsigned long long)index);
		page->flags |= PG_UPTODATE;
	} else if (!(page->flags & PG_UPTODATE)) {
		// Cache miss: populate front bytes from disk, then zero the tail
		if (map->imops && map->imops->readpage) {
			int res = map->imops->readpage(inode, page);
			if (res < 0) {
				log_error("Readpage failed (index=%llu, file_off=0x%llx) err=%d",
					  (unsigned long long)index,
					  (unsigned long long)file_off,
					  res);
				put_page(page);
				imap_remove(map, page);
				return -EIO;
			}
			void* kvaddr = (void*)PHYS_TO_HHDM(page_to_phys(page));
			memset((char*)kvaddr + to_read, 0, tail);
			log_debug("FILE: readpage filled %zu bytes, zeroed %zu (index=%llu)",
				  to_read,
				  tail,
				  (unsigned long long)index);
		} else {
			// No readpage -> we must synthesize the page (rare)
			void* kvaddr = (void*)PHYS_TO_HHDM(page_to_phys(page));
			memset(kvaddr, 0, PAGE_SIZE);
			log_warn("FILE: no readpage op; zeroed whole page (index=%llu)", (unsigned long long)index);
		}
		page->flags |= PG_UPTODATE;

	} else {
		// Cache hit. Defensively ensure the last page's tail is zero.
		if (to_read < PAGE_SIZE) {
			void* kvaddr = (void*)PHYS_TO_HHDM(page_to_phys(page));
			memset((char*)kvaddr + to_read, 0, tail);
			log_debug("FILE: cache hit; ensured tail-zero %zu bytes (index=%llu)",
				  tail,
				  (unsigned long long)index);
		} else {
			log_debug("FILE: cache hit; full page content present (index=%llu)", (unsigned long long)index);
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
 * @brief Populates a single page according to its VMA policy.
 *
 * @param vas Target address space.
 * @param vaddr Virtual address, at any offset within the page.
 *
 * @return 0 on success or if already present. -EFAULT if no VMA covers
 * @p vaddr. -EINVAL on bad arguments. Other negative errors come from
 * helper functions.
 *
 * @note May sleep. May perform I/O for file-backed regions.
 *
 * Does nothing if already mapped. Otherwise, locates the covering VMA and
 * delegates to the matching populate helper for anonymous or file-backed
 * pages. PTE permissions derive from the VMA. Private file mappings are
 * armed for copy-on-write on first write.
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

	const char* kind = (mr->kind == MR_FILE) ? "FILE" : (mr->kind == MR_ANON) ? "ANON" : "DEVICE";
	char prot_str[4] = { (mr->prot & PROT_READ) ? 'r' : '-',
			     (mr->prot & PROT_WRITE) ? 'w' : '-',
			     (mr->prot & PROT_EXEC) ? 'x' : '-',
			     '\0' };
	log_debug("VMA: [%016lx..%016lx) kind=%s prot=%s flags=0x%lx private=%d",
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
 * @brief Writes data to a virtual memory region.
 *
 * @param vas Address space that contains the target virtual memory.
 * @param vaddr Starting virtual address to write to.
 * @param data Source data buffer to copy from.
 * @param len Number of bytes to write.
 *
 * Translates virtual addresses to physical addresses page by page. Splits
 * writes that span multiple pages into page-aligned chunks.
 *
 * @note Assumes all target virtual pages are already mapped and
 * accessible. Performs no page-fault handling.
 */
void vmm_write_region(struct address_space* vas, vaddr_t vaddr, const void* data, size_t len)
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
		size_t bytes_to_copy = (len < bytes_in_page) ? len : bytes_in_page;

		// Translate virtual to physical address
		// TODO: Make sure this returns correct address
		paddr_t paddr = get_phys_addr(vas->pml4, vaddr);

		if (paddr == 0) {
			int rc = vmm_populate_one(vas, vaddr);
			if (rc < 0) {
				log_error("vmm_populate_one failed for vaddr 0x%lx: %d", vaddr, rc);
				return;
			}
			paddr = get_phys_addr(vas->pml4, vaddr);
			log_debug("Populated page for vaddr 0x%lx, got paddr 0x%lx", vaddr, paddr);
		}

		vaddr_t kernel_vaddr = PHYS_TO_HHDM(paddr);

		log_debug("Writing %zu bytes to vaddr 0x%lx (phys 0x%lx)", bytes_to_copy, vaddr, paddr);
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
 * @brief Drops empty page-table nodes under a virtual address.
 *
 * @param table Page-table at the current walk level.
 * @param level Walk level. 0 is PML4, 1 is PDPT, 2 is PD, 3 is PT (leaf).
 * @param vaddr Virtual address that anchors the walk.
 *
 * @return True if @p table is empty after pruning. False otherwise.
 *
 * @note May sleep, since it frees tables. Takes no locks. The caller must
 * synchronize page-table access and any needed TLB shootdowns.
 *
 * Recurses toward the leaf for @p vaddr. If a child becomes empty, clears
 * the parent entry and frees the child table. Prunes only non-present
 * subtrees and does not handle huge-page mappings.
 */
static bool prune_page_table_recursive(uint64_t* table, int level, uintptr_t vaddr)
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
		uint64_t* child_table = (uint64_t*)PHYS_TO_HHDM(entry & PAGE_FRAME_MASK);
		if (prune_page_table_recursive(child_table, level + 1, vaddr)) {
			table[index] = 0; // Clear the entry if child table was pruned
			_free_page_table(child_table);
			log_debug("Freed PT at level %d (vaddr: 0x%lx)", level, vaddr);
		}
	}

	return is_table_empty((pgd_t*)table);
}

/**
 * @brief Returns the leaf PTE for a virtual address, optionally creating
 * tables.
 *
 * @param pml4 Top-level page table.
 * @param vaddr Virtual address to walk. Must be canonical.
 * @param create Allocates intermediate tables if missing.
 * @param flags Flags to apply to newly created non-leaf entries. Include
 * PRESENT.
 *
 * @return Pointer to the leaf PTE, or NULL if absent and @p create is false.
 *
 * @note May sleep if @p create is set and the allocator sleeps. Takes no
 * locks. The caller must hold the page-table lock and manage IRQ state.
 * Does not set huge pages.
 */
static pte_t* walk_page_table(pgd_t* pml4, uptr vaddr, bool create, flags_t flags)
{
	if (create && (flags & PAGE_PRESENT) == 0) {
		log_warn("walk_page_table creating an entry WITHOUT PAGE_PRESENT! flags: 0x%lx", flags);
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
		pml4[pml4_i].pgd = (u64)HHDM_TO_PHYS(_alloc_page_table(AF_KERNEL)) | flags;
	}

	// Get the PDPT from the PML4 entry
	pud_t* pdpt = (pud_t*)PHYS_TO_HHDM(pml4[pml4_i].pgd & ~FLAGS_MASK); // Mask off flags
	uint64_t pdpt_i = _pdpt_index(vaddr);
	if ((pdpt[pdpt_i].pud & PAGE_PRESENT) == 0) {
		if (!create) return NULL;
		pdpt[pdpt_i].pud = (u64)HHDM_TO_PHYS(_alloc_page_table(AF_KERNEL)) | flags;
	}

	// Get the PD from the PDPT entry
	pmd_t* pd = (pmd_t*)PHYS_TO_HHDM(pdpt[pdpt_i].pud & ~FLAGS_MASK); // Mask off flags
	uint64_t pd_i = _pd_index(vaddr);
	if ((pd[pd_i].pmd & PAGE_PRESENT) == 0) {
		if (!create) return NULL;
		pd[pd_i].pmd = (u64)HHDM_TO_PHYS(_alloc_page_table(AF_KERNEL)) | flags;
	}

	// Get the PT from the PD entry
	pte_t* pt = (pte_t*)PHYS_TO_HHDM(pd[pd_i].pmd & ~FLAGS_MASK);
	uint64_t pt_i = _pt_index(vaddr);

	// Return the pointer to the page table entry
	return pt + pt_i;
}

/**
 * @brief Maps a bootloader memmap span into kernel space.
 *
 * @param pml4 Top-level page table.
 * @param entry Boot info memory-map entry to mirror.
 * @param k_vstart Kernel virtual start.
 * @param k_pstart Kernel physical start.
 * @param k_size Kernel size.
 *
 * @note Early-boot mapping helper. Assumes single-CPU init. Takes no locks.
 *
 * Maps the span into the HHDM. For EXECUTABLE and MODULES entries, also
 * maps an executable alias at the kernel virtual base plus offset.
 */
static void
map_memmap_entry(pgd_t* pml4, struct bootinfo_memmap_entry* entry, uptr k_vstart, uptr k_pstart, size_t k_size)
{
	flags_t flags;
	switch (entry->type) {
	case LIMINE_MEMMAP_USABLE:
	case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
	case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
		flags = PAGE_PRESENT | PAGE_WRITE | CACHE_WRITE_BACK | PAGE_NO_EXECUTE;
		break;
	case LIMINE_MEMMAP_FRAMEBUFFER:
		flags = PAGE_PRESENT | PAGE_WRITE | CACHE_WRITE_COMBINING | PAGE_NO_EXECUTE;
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
 * @brief Dumps the PML4 through PT entries for a virtual address.
 *
 * @param pml4 Top-level page table (virtual, through the HHDM).
 * @param vaddr Address to trace.
 *
 * @note Debug-only. Read-only walk. IRQ-safe. Takes no locks. Tolerates
 * races.
 *
 * Prints each level and notes huge-page stops or not-present entries.
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
 * @brief Handles a not-present page fault for the current task.
 *
 * @param r Fault frame registers.
 *
 * @return 0 on success, or -errno from page population.
 *
 * @note Runs on the page-fault path. Must not sleep beyond what the
 * handler allows.
 *
 * Derives the access type from the page-fault error code, checks VMA
 * permissions, and populates a single page with vmm_populate_one().
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
 * @brief x86-64 page-fault top-half handler.
 *
 * @param r Fault frame registers.
 *
 * @note Runs as a fault handler. IRQ state depends on entry. Reentrancy is
 * not expected.
 *
 * Routes not-present faults to demand paging, handles copy-on-write on
 * write faults, and calls page_fault_fail() on irrecoverable errors.
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

	log_debug("PF: cr2=0x%lx rip=0x%lx ec=0x%lx [P=%d W=%d U=%d I=%d RSVD=%d] "
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
			bool phys_shared = atomic_read(&shared_page->mapcount) > 1;
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
 * @brief Fatal page-fault handler. Does not return.
 *
 * @param r Fault frame registers.
 *
 * @note Logs synchronously and panics.
 *
 * Dumps the faulting task, registers, and a page-table walk before halting.
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
		log_error("Violation: This was a write operation to a read-only page.");
	} else {
		log_error("Violation: This was a read operation.");
	}
	if (user) {
		log_error("Context: The fault occurred in user-mode.");
	} else {
		log_error("Context: The fault occurred in kernel-mode.");
	}
	if (reserved) {
		log_error("Details: A reserved bit was set in a page directory entry.");
	}
	if (id) {
		log_error("Details: The fault was caused by an instruction fetch.");
	}

	log_error("General registers:");
	log_error("RIP: %lx, RSP: %lx, RBP: %lx", r->rip, r->rsp, r->rbp);
	log_error("RAX: %lx, RBX: %lx, RCX: %lx, RDX: %lx", r->rax, r->rbx, r->rcx, r->rdx);
	log_error("RDI: %lx, RSI: %lx, RFLAGS: %lx, DS: %lx", r->rdi, r->rsi, r->rflags, r->ds);
	log_error("CS: %lx, SS: %lx", r->cs, r->ss);
	log_error("R8: %lx, R9: %lx, R10: %lx, R11: %lx", r->r8, r->r9, r->r10, r->r11);
	log_error("R12: %lx, R13: %lx, R14: %lx, R15: %lx", r->r12, r->r13, r->r14, r->r15);

	log_page_table_walk((uint64_t*)PHYS_TO_HHDM(cr3), fault_addr);

	// This calls console_flush()
	panic("Page Fault");
}

#if defined(HELIOS_TESTS)
#include "kernel/ktest.h"
/**
 * @brief Tests pruning of a single mapping in the page table.
 *
 * @return 0 on success, or 1 on failure.
 */
KTEST(test_prune_single_mapping)
{
	int rc = 1;

	uint64_t* pml4 = _alloc_page_table(AF_KERNEL);
	KTEST_ASSERT_NE(pml4, nullptr);

	uintptr_t vaddr = 0x00007FFFFFFFE000; // Arbitrary, canonical, aligned
	void* page = get_free_page(AF_KERNEL);
	KTEST_ASSERT_NE_GOTO(page, nullptr, err_free_table);
	uintptr_t paddr = (uintptr_t)HHDM_TO_PHYS(page);

	log_info("Mapping page: virt=0x%lx -> phys=0x%lx", vaddr, paddr);
	KTEST_ASSERT_EQ_GOTO(vmm_map_page((pgd_t*)pml4, vaddr, paddr, PAGE_PRESENT | PAGE_WRITE | CACHE_WRITE_BACK),
			     0,
			     err_free_table);

	log_info("Unmapping page: 0x%lx", vaddr);
	KTEST_ASSERT_EQ_GOTO(vmm_unmap_page((pgd_t*)pml4, vaddr), 0, err_free_page);

	log_info("Pruning page tables for vaddr 0x%lx", vaddr);
	prune_page_tables(pml4, vaddr);

	size_t pml4_i = get_table_index(0, vaddr);
	KTEST_ASSERT_EQ_GOTO(pml4[pml4_i], 0, err_free_page);
	log_info("PML4 entry cleared: pruning successful");

	rc = 0;

err_free_page:
	free_page((void*)PHYS_TO_HHDM(paddr));
err_free_table:
	_free_page_table(pml4);
	return rc;
}

KTEST(test_device_region_unmap_no_page_touch)
{
	int rc = 1;
	uint64_t* pml4 = _alloc_page_table(AF_KERNEL);
	KTEST_ASSERT_NE(pml4, nullptr);

	struct address_space vas = { 0 };
	list_init(&vas.mr_list);
	rwsem_init(&vas.vma_lock);
	spin_init(&vas.pgt_lock);
	vas_set_pml4(&vas, (pgd_t*)pml4);

	void* page_va = get_free_page(AF_KERNEL);
	KTEST_ASSERT_NE_GOTO(page_va, nullptr, err_free_table);

	uintptr_t paddr = (uintptr_t)HHDM_TO_PHYS(page_va);
	struct page* pg = phys_to_page(paddr);
	int ref_before = atomic_read(&pg->ref_count);

	uintptr_t vaddr = 0x00007FFFFFFFE000;
	KTEST_ASSERT_EQ_GOTO(
		map_device_region(&vas, vaddr, vaddr + PAGE_SIZE, paddr, PROT_READ | PROT_WRITE, MAP_SHARED),
		0,
		err_free_page);

	struct memory_region* mr = get_region(&vas, vaddr);
	KTEST_ASSERT_NE_GOTO(mr, nullptr, err_free_page);

	pte_t* pte = walk_page_table((pgd_t*)pml4, vaddr, false, 0);
	KTEST_ASSERT_NE_GOTO(pte, nullptr, err_unmap);
	KTEST_ASSERT_EQ_GOTO(pte->pte & PAGE_PRESENT, PAGE_PRESENT, err_unmap);
	KTEST_ASSERT_EQ_GOTO(pte->pte & (PTE_PAT | PAGE_PWT), (PTE_PAT | PAGE_PWT), err_unmap);
	KTEST_ASSERT_EQ_GOTO(pte->pte & PAGE_NO_EXECUTE, PAGE_NO_EXECUTE, err_unmap);
	KTEST_ASSERT_EQ_GOTO(atomic_read(&pg->ref_count), ref_before, err_unmap);

	unmap_region(&vas, mr);

	pte = walk_page_table((pgd_t*)pml4, vaddr, false, 0);
	KTEST_ASSERT_TRUE_GOTO(!pte || !(pte->pte & PAGE_PRESENT), err_free_page);
	KTEST_ASSERT_EQ_GOTO(atomic_read(&pg->ref_count), ref_before, err_free_page);

	rc = 0;
	goto err_free_page;

err_unmap:
	unmap_region(&vas, mr);
err_free_page:
	free_page(page_va);
err_free_table:
	_free_page_table(pml4);
	return rc;
}
#endif
