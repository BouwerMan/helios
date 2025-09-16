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

#include <stdint.h>
#include <uapi/helios/errno.h>
#include <uapi/helios/mman.h>

#undef LOG_LEVEL
#define LOG_LEVEL 0
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

/*******************************************************************************
* Private Function Prototypes
*******************************************************************************/

/**
 * @brief Maps a memory map entry into the virtual memory space.
 *
 * @param pml4 Pointer to the PML4 table used for virtual memory mapping.
 * @param entry Pointer to the memory map entry to be mapped.
 * @param exe_virt_base Base virtual address for executable mappings.
 */
static void map_memmap_entry(u64* pml4,
			     struct bootinfo_memmap_entry* entry,
			     uptr exe_virt_base);
/**
 * @brief Handles page faults by logging the faulting address and attempting to resolve it.
 *
 * @param r Pointer to the registers structure containing the faulting address.
 */
static void page_fault(struct registers* r);

[[noreturn]]
static void page_fault_fail(struct registers* r);

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
static pte_t*
walk_page_table(pgd_t* pml4, uptr vaddr, bool create, flags_t flags);

static void log_page_table_walk(u64* pml4, uptr vaddr);

/**
 * @brief Retrieves the index for a specific level in the page table hierarchy.
 *
 * @param level  The level in the page table hierarchy (0 = PML4, 3 = PT).
 * @param vaddr  The virtual address to calculate the index for.
 * @return       The index for the specified level, or (size_t)-1 if the level is invalid.
 */
static size_t get_table_index(int level, uintptr_t vaddr);

/**
 * @brief Checks if a page table is empty.
 *
 * @param table  Pointer to the page table to check.
 * @return       True if the table is empty, false otherwise.
 */
static bool is_table_empty(uint64_t* table);

/**
 * @brief Recursively prunes empty page tables in the hierarchy.
 *
 * @param table  Pointer to the current page table.
 * @param level  Current level in the page table hierarchy (0 = PML4, 3 = leaf).
 * @param vaddr  Virtual address to prune from.
 *
 * @return       True if the table is empty after pruning, false otherwise.
 */
static bool
prune_page_table_recursive(uint64_t* table, int level, uintptr_t vaddr);

/**
 * @brief Invalidates a single page in the TLB (Translation Lookaside Buffer).
 *
 * @param vaddr The virtual address of the page to invalidate.
 */
static inline void invalidate(uintptr_t vaddr)
{
	__asm__ volatile("invlpg (%0)" ::"r"(vaddr) : "memory");
}

static inline size_t _page_table_index(uptr vaddr, int shift)
{
	return (vaddr >> shift) & 0x1FF;
}

#define _pml4_index(vaddr) _page_table_index(vaddr, 39)
#define _pdpt_index(vaddr) _page_table_index(vaddr, 30)
#define _pd_index(vaddr)   _page_table_index(vaddr, 21)
#define _pt_index(vaddr)   _page_table_index(vaddr, 12)

static inline void* _alloc_page_table(aflags_t flags)
{
	return (void*)get_free_pages(flags, PML4_SIZE_PAGES);
}

static inline void _free_page_table(void* table)
{
	free_pages(table, PML4_SIZE_PAGES);
}

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
 * @brief Initializes the virtual memory manager (VMM).
 *
 * This function sets up the kernel's address space by creating a new PML4
 * table and mapping memory regions based on the boot information provided
 * by the bootloader. It ensures that the kernel's memory layout is properly
 * initialized and ready for use.
 *
 * Steps:
 * 1. Validates the boot information structure.
 * 2. Allocates a new PML4 table for the kernel.
 * 3. Iterates through the memory map entries provided by the bootloader and
 *    maps them into the kernel's address space.
 * 4. Loads the new PML4 table into the CR3 register to activate the address space.
 */
void vmm_init()
{
	isr_install_handler(PAGE_FAULT, page_fault);

	// Init new address space, then copy from limine
	struct bootinfo* bootinfo = &kernel.bootinfo;
	if (!bootinfo->valid) panic("bootinfo marked not valid");

	kernel.pml4 = _alloc_page_table(AF_KERNEL);
	log_debug("Current PML4: %p", (void*)kernel.pml4);
	for (size_t i = 0; i < bootinfo->memmap_entry_count; i++) {
		struct bootinfo_memmap_entry* entry = &bootinfo->memmap[i];
		map_memmap_entry(kernel.pml4,
				 entry,
				 bootinfo->executable.virtual_base);
	}

	vmm_load_cr3(HHDM_TO_PHYS(kernel.pml4));
}

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
 * @brief Maps a virtual address to a physical address in the page table.
 *
 * This function maps a virtual address to a physical address in the page table
 * with the specified flags. It ensures that both the virtual and physical
 * addresses are page-aligned. If the mapping already exists, or if there is
 * an alignment issue, the function returns an error.
 *
 * @param pml4   Pointer to the PML4 table.
 * @param vaddr  Virtual address to map.
 * @param paddr  Physical address to map to.
 * @param flags  Flags for the page table entry (e.g., PAGE_PRESENT, PAGE_WRITE).
 *
 * @return       0 on success, -1 on failure (e.g., misalignment or mapping issues).
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
	return 0;
}

/**
 * @brief Unmaps a virtual address from the page table.
 *
 * This function removes the mapping of a virtual address from the page table.
 * It ensures that the virtual address is page-aligned and checks if the page
 * is already unmapped. If the page is mapped, it clears the page table entry,
 * prunes empty page tables, and invalidates the TLB entry for the virtual address.
 *
 * @param pml4   Pointer to the PML4 table.
 * @param vaddr  Virtual address to unmap.
 *
 * @return       0 on success, -1 on failure (e.g., misalignment).
 *               Returns 0 if the page was already unmapped.
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
	put_page(page);
	log_debug("Unmapping page at vaddr: %lx, ref count: %d",
		  vaddr,
		  atomic_read(&page->ref_count));

	pte->pte = 0;

	// TODO: Rework this to use the new typedefs (I am lazy)
	prune_page_tables((uint64_t*)pml4, vaddr);
	invalidate(vaddr);

	return 0;
}

/**
 * @brief Prunes empty page tables recursively.
 *
 * This function traverses the page table hierarchy and removes empty tables
 * starting from the specified virtual address. It ensures that unused page
 * tables are freed to conserve memory.
 *
 * @param pml4   Pointer to the PML4 table.
 * @param vaddr  Virtual address to start pruning from.
 *
 * @return       0 on success.
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

paddr_t get_phys_addr(pgd_t* pml4, vaddr_t vaddr)
{
	u64 low = vaddr & FLAGS_MASK;
	pte_t* pte = walk_page_table(pml4, vaddr & PAGE_FRAME_MASK, false, 0);
	if (!pte || !(pte->pte & PAGE_PRESENT)) {
		return 0;
	}

	paddr_t paddr = pte->pte & PAGE_FRAME_MASK;

	return paddr + low;
}

/**
 * vmm_map_anon_region - Map a memory region by allocating new pages
 * @vas: Target address space to map the region into
 * @mr: Memory region descriptor containing virtual address range and permissions
 *
 * This function maps a virtual memory region by allocating new physical pages
 * for each page in the region's virtual address range. Each page is mapped
 * with permissions derived from the memory region's protection flags.
 *
 * The function assumes the memory region (mr) is fully initialized with valid
 * start/end addresses and protection flags. The address space parameter (vas)
 * is passed to handle potential edge cases where add_region() hasn't been
 * called before this mapping operation.
 *
 * Page permissions are constructed as follows:
 * - PAGE_PRESENT | PAGE_USER are always set
 * - PAGE_WRITE is set if PROT_WRITE is in mr->prot
 * - PAGE_NO_EXECUTE is set if PROT_EXEC is NOT in mr->prot
 *
 * Return: 0 on success, negative error code on failure
 * Errors: -EINVAL if vas or mr is NULL
 *         Other negative values from vmm_map_page() failures
 *
 * Note: On failure, all successfully mapped pages are automatically unmapped
 *       during cleanup to prevent memory leaks.
 */
int vmm_map_anon_region(struct address_space* vas, struct memory_region* mr)
{
	int err = 0;
	vaddr_t v = 0;

	if (!vas || !mr) {
		return -EINVAL;
	}

	// NOTE: Not sure if I should assume PAGE_USER
	flags_t flags = PAGE_PRESENT | PAGE_USER;
	flags |= mr->prot & PROT_WRITE ? PAGE_WRITE : 0;
	flags |= mr->prot & PROT_EXEC ? 0 : PAGE_NO_EXECUTE;

	for (v = mr->start; v < mr->end; v += PAGE_SIZE) {
		struct page* page = alloc_page(AF_NORMAL);
		paddr_t paddr = page_to_phys(page);

		err = vmm_map_page(vas->pml4, v, paddr, flags);
		if (err < 0) {
			goto clean;
		}
	}

	return 0;

clean:
	log_error("Failed to map region");

	vaddr_t cleanup_end = v; // Don't include the failed page
	for (v = mr->start; v < cleanup_end; v += PAGE_SIZE) {
		// Find the original flags to restore them
		int res = vmm_unmap_page(vas->pml4, v);
		if (res < 0) {
			panic("Could not cleanup vmm_map_region");
		}
	}

	return err;
}

/**
 * vmm_fork_region - Fork a memory region with copy-on-write semantics
 * @dest_vas: Destination address space to create the forked region in
 * @src_mr: Source memory region to fork from
 *
 * This function implements copy-on-write (COW) memory region forking for
 * process creation. Instead of copying physical pages immediately, both
 * parent and child processes share the same physical pages, with write
 * access removed to trigger page faults on modification attempts.
 *
 * The forking process:
 * 1. Maps each present page from source region into destination address space
 * 2. Removes write permissions from both source and destination mappings
 * 3. Increments reference count on shared physical pages
 * 4. Preserves original page permissions for pages that were already read-only
 *
 * Return: 0 on success, negative error code on failure
 * Errors: -EINVAL if dest_vas or src_mr is NULL
 *         -EFAULT if source page is not present or accessible
 *         Other negative values from vmm_map_page() or vmm_protect_page()
 *
 * Note: On failure, all successfully mapped pages in destination are unmapped
 *       and source page protections are restored to prevent inconsistent state.
 */
int vmm_fork_region(struct address_space* dest_vas,
		    struct memory_region* src_mr)
{
	// TODO: Base sharing policy on src_mr
	int err = 0;
	vaddr_t v = 0;

	if (!dest_vas || !src_mr) {
		return -EINVAL;
	}

	struct address_space* src_vas = src_mr->owner;

	for (v = src_mr->start; v < src_mr->end; v += PAGE_SIZE) {
		pte_t* src_pte = walk_page_table(src_vas->pml4, v, false, 0);
		if (!src_pte || !(src_pte->pte & PAGE_PRESENT)) {
			// Because of demand paging, this may not be an error
			// if (src_mr->flags & MAP_ANONYMOUS) {
			// 	err = -EFAULT;
			// 	log_error(
			// 		"Source page not present at vaddr 0x%lx",
			// 		v);
			// 	goto clean;
			// } else {
			// 	continue;
			// }

			// TODO: Rework cleaning because it kinda funky rn
			continue;
		}

		paddr_t p = src_pte->pte & PAGE_FRAME_MASK;
		flags_t current_flags = (src_pte->pte & FLAGS_MASK);
		flags_t new_flags = current_flags & ~PAGE_WRITE;

		err = vmm_map_page(dest_vas->pml4, v, p, new_flags);
		if (err < 0) {
			goto clean;
		}

		struct page* page = phys_to_page(p);
		get_page(page);

		// Skip write protecting if page is already read only
		if (!(current_flags & PAGE_WRITE)) continue;

		err = vmm_protect_page(src_vas, v, new_flags);
		if (err < 0) {
			goto clean;
		}
	}

	return 0;

clean:
	log_error("Failed to fork region: %d", err);

	vaddr_t cleanup_end = v; // Don't include the failed page
	for (v = src_mr->start; v < cleanup_end; v += PAGE_SIZE) {
		// Find the original flags to restore them
		pte_t* src_pte = walk_page_table(src_vas->pml4, v, false, 0);
		if (src_pte && (src_pte->pte & PAGE_PRESENT)) {
			flags_t original_flags = (src_pte->pte & FLAGS_MASK) |
						 PAGE_WRITE;
			// Restore parent's write access if it was a writable page
			if (src_mr->prot & PROT_WRITE) {
				vmm_protect_page(src_vas, v, original_flags);
			}
		}

		int res = vmm_unmap_page(dest_vas->pml4, v);
		if (res < 0) {
			panic("Could not cleanup vmm_fork_region");
		}
	}

	return err;
}

/**
 * vmm_unmap_region - Unmap all pages within a memory region
 * @vas: Address space containing the memory region to unmap
 * @mr: Memory region descriptor specifying the virtual address range to unmap
 *
 * This function unmaps all virtual pages within the specified memory region
 * by iterating through each page-aligned virtual address and removing its
 * mapping from the page tables. This operation effectively makes the virtual
 * address range inaccessible and may free associated physical pages depending
 * on reference counting.
 *
 * Note: The memory region structure (mr) may not have its owner field populated,
 * so the address space must be passed explicitly as a parameter.
 *
 * Return: 0 on success, negative error code on failure
 * Errors: Propagates error codes from vmm_unmap_page() if individual page
 *         unmapping fails (e.g., invalid virtual address, page table corruption)
 */
int vmm_unmap_region(struct address_space* vas, struct memory_region* mr)
{
	// TODO: This shouldn't return err probably. If we do error that could
	// just mean we are doing lazy alloc and didn't allocate a page. Which
	// isn't a failure.
	for (vaddr_t v = mr->start; v < mr->end; v += PAGE_SIZE) {
		int err = vmm_unmap_page(vas->pml4, v);
		if (err < 0) {
			return err;
		}
	}

	return 0;
}

/**
 * vmm_protect_page - Change memory protection flags for a single virtual page
 * @vas: Address space containing the page to modify
 * @vaddr: Virtual address of the page to change (must be page-aligned)
 * @new_prot: New protection flags to apply to the page
 *
 * This function modifies the memory protection attributes of a single virtual
 * page by updating its page table entry (PTE). The function preserves the
 * physical address mapping while updating only the permission bits.
 *
 * Return: 0 on success, negative error code on failure
 * Errors: -EFAULT if the virtual address is not mapped or page is not present
 *
 * Note: The new protection flags should include appropriate architecture-specific
 *       bits (e.g., PAGE_PRESENT, PAGE_USER) as this function performs a direct
 *       flag replacement rather than selective bit modification.
 */
int vmm_protect_page(struct address_space* vas, vaddr_t vaddr, flags_t new_prot)
{
	pte_t* pte = walk_page_table(vas->pml4, vaddr, false, 0);
	if (!pte || !(pte->pte & PAGE_PRESENT)) {
		return -EFAULT;
	}

	uptr paddr = pte->pte & PAGE_FRAME_MASK;
	pte->pte = paddr | (new_prot & FLAGS_MASK);

	invalidate(vaddr);
	return 0;
}

int __vmm_populate_one_anon(struct address_space* vas,
			    struct memory_region* mr,
			    vaddr_t vaddr)
{
	if (!vas || !mr) return -EINVAL;

	flags_t flags = flags_from_mr(mr);

	vaddr_t va = vaddr & ~(PAGE_SIZE - 1);

	struct page* page = alloc_zeroed_page(AF_NORMAL);
	if (!page) {
		log_error("OOM allocating anon page for vaddr=0x%lx",
			  (unsigned long)vaddr);
		return -ENOMEM;
	}

	paddr_t p = page_to_phys(page);

	int err = vmm_map_page(vas->pml4, va, p, flags);
	if (err < 0) {
		log_error("Map anon vaddr=0x%lx paddr=0x%lx failed err=%d",
			  (unsigned long)vaddr,
			  (unsigned long)p,
			  err);
		__free_page(page);
		return err;
	}

	log_debug("Mapped ANON page vaddr=0x%lx paddr=0x%lx prot_flags=0x%lx",
		  (unsigned long)vaddr,
		  (unsigned long)p,
		  (unsigned long)flags);

	return 0;
}

int __vmm_populate_one_file(struct address_space* vas,
			    struct memory_region* mr,
			    vaddr_t vaddr)
{
	flags_t flags = flags_from_mr(mr);

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
	int err = vmm_map_page(vas->pml4, vaddr, page_to_phys(page), flags);
	if (err < 0) {
		log_error(
			"PF: map FILE vaddr=0x%lx paddr=0x%lx prot_flags=0x%lx failed err=%d "
			"(index=%llu file_off=0x%llx)",
			(unsigned long)vaddr,
			(unsigned long)page_to_phys(page),
			(unsigned long)flags,
			err,
			(unsigned long long)index,
			(unsigned long long)file_off);
		imap_remove(map, page);
		return err;
	}

	unlock_page(page);
	log_debug("PF: done vaddr=0x%lx page.ref=%d",
		  (unsigned long)vaddr,
		  atomic_read(&page->ref_count));
	return 0;
}

// Populate one page at 'vaddr' in 'vas' according to its VMA.
// Returns 0 on success; <0 on error (e.g., no VMA, perms, OOM, I/O error).
int vmm_populate_one(struct address_space* vas, vaddr_t vaddr)
{
	if (!vas) return -EINVAL;
	if (!is_within_vas(vas, vaddr)) return -EFAULT;

	vaddr_t va = vaddr & ~(PAGE_SIZE - 1);

	paddr_t p = get_phys_addr(vas->pml4, va);
	if (p) {
		return 0; // Addr exists
	}

	struct memory_region* mr = get_region(vas, va);
	if (!mr) {
		log_error("No memory region for vaddr 0x%lx", vaddr);
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
		return __vmm_populate_one_anon(vas, mr, va);
	} else if (mr->kind == MR_FILE) {
		return __vmm_populate_one_file(vas, mr, va);
	} else {
		log_error("Unknown memory region kind %d", mr->kind);
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

static size_t get_table_index(int level, uintptr_t vaddr)
{
	switch (level) {
	case 0:	 return _pml4_index(vaddr);
	case 1:	 return _pdpt_index(vaddr);
	case 2:	 return _pd_index(vaddr);
	case 3:	 return _pt_index(vaddr);
	default: return (size_t)-1; // Invalid level
	}
}

static bool is_table_empty(uint64_t* table)
{
	// TODO: turn this into a memcmp (ideally architecture specific with rep cmpsb)
	for (size_t i = 0; i < PML4_ENTRIES; i++) {
		if (table[i] != 0) return false; // Found non-empty entry
	}
	return true;
}

/**
 * @brief Recursively prunes empty page tables in the hierarchy.
 *
 * This function traverses the page table hierarchy starting from the given
 * level and virtual address. It checks if the current table entry is present
 * and, if not, determines whether the table is empty. If the table is empty,
 * it clears the entry and frees the associated memory. For non-leaf levels,
 * the function recurses into child tables to prune them as well.
 *
 * @param table  Pointer to the current page table.
 * @param level  Current level in the page table hierarchy (0 = PML4, 3 = leaf).
 * @param vaddr  Virtual address to prune from.
 *
 * @return       True if the table is empty after pruning, false otherwise.
 */
static bool
prune_page_table_recursive(uint64_t* table, int level, uintptr_t vaddr)
{
	size_t index = get_table_index(level, vaddr);
	uintptr_t entry = table[index];

	// If the entry is not present, return early
	if ((entry & PAGE_PRESENT) == 0) {
		return is_table_empty(table);
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

	return is_table_empty(table);
}

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

static void map_memmap_entry(uint64_t* pml4,
			     struct bootinfo_memmap_entry* entry,
			     uintptr_t exe_virt_base)
{
	flags_t flags;
	switch (entry->type) {
	case LIMINE_MEMMAP_USABLE:
	case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
		flags = PAGE_PRESENT | PAGE_WRITE | CACHE_WRITE_BACK |
			PAGE_NO_EXECUTE;
		break;
	case LIMINE_MEMMAP_FRAMEBUFFER:
		flags = PAGE_PRESENT | PAGE_WRITE | CACHE_WRITE_COMBINING |
			PAGE_NO_EXECUTE;
		break;
	case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
		flags = PAGE_PRESENT | PAGE_WRITE | CACHE_WRITE_BACK;
		break;
	default: return;
	}

	uintptr_t start = entry->base;
	uintptr_t end = entry->base + entry->length;
	log_debug("Mapping [%lx-%lx), type: %lu", start, end, entry->type);
	for (size_t phys = start; phys < end; phys += PAGE_SIZE) {
		vmm_map_page((pgd_t*)pml4, PHYS_TO_HHDM(phys), phys, flags);

		// Manually map executable areas
		if (entry->type != LIMINE_MEMMAP_EXECUTABLE_AND_MODULES)
			continue;
		uintptr_t exe_virt = (phys - start) + exe_virt_base;
		vmm_map_page((pgd_t*)pml4,
			     exe_virt,
			     phys,
			     flags & ~PAGE_NO_EXECUTE);
	}
}

static void log_page_table_walk(u64* pml4, uptr vaddr)
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
	if (pde & PAGE_HUGE) {
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

static int do_demand_paging(struct registers* r)
{
	struct task* task = get_current_task();
	struct address_space* vas = task->vas;

	uint64_t fault_addr;
	__asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));

	vaddr_t vaddr = align_down_page(fault_addr);
	struct memory_region* mr = get_region(vas, vaddr);
	if (!mr) {
		log_error("No VMA covers vaddr=0x%lx", (unsigned long)vaddr);
		return -ENOENT;
	}

	// Region identity snapshot
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

	bool need_exec = r->err_code & 0x10;
	bool need_write = r->err_code & 0x2;
	bool need_read = !need_write;

	if (need_exec && !(mr->prot & PROT_EXEC)) {
		log_error("NX violation at vaddr=0x%lx in %s VMA",
			  (unsigned long)vaddr,
			  kind);
		return -EACCES;
	}
	if (need_write && !(mr->prot & PROT_WRITE)) {
		log_error("Write disallowed at vaddr=0x%lx in %s VMA",
			  (unsigned long)vaddr,
			  kind);
		return -EACCES;
	}
	if (need_read && !(mr->prot & PROT_READ)) {
		log_error("Read disallowed at vaddr=0x%lx in %s VMA",
			  (unsigned long)vaddr,
			  kind);
		return -EACCES;
	}

	return vmm_populate_one(vas, vaddr);
}

static void page_fault(struct registers* r)
{
	if (!is_scheduler_init()) {
		goto fail;
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
		// This is not a CoW fault. It might be a real error (like a segfault)
		// or something else like demand paging. For now, we'll treat it as a failure.
		int dc = do_demand_paging(r);
		if (dc == 0) {
			goto pass;
		}
		log_error("Demand paging failed with err=%d", dc);
		goto fail;
	}
	if (!is_write_fault) {
		goto fail;
	}

	vaddr_t page_aligned_addr = fault_addr & PAGE_FRAME_MASK;

	if (vas->pml4_phys != cr3) {
		goto fail;
	}

	log_debug("Faulted in address_space %lx", cr3);
	// address_space_dump(vas);

	pte_t* pte = walk_page_table(vas->pml4, page_aligned_addr, false, 0);
	if (!pte) {
		goto fail;
	}

	paddr_t shared_paddr = pte->pte & PAGE_FRAME_MASK;
	struct page* shared_page = phys_to_page(shared_paddr);

	if (atomic_read(&shared_page->ref_count) > 1) {
		// Case 1: We must copy since this is shared

		struct page* new_page = alloc_page(AF_NORMAL);
		if (!new_page) {
			log_error("OOM during CoW fault!");
			goto fail;
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
	} else {
		// Case 2: We are last user, therefore no need to copy.
		// Just make it writeable

		flags_t new_flags = (pte->pte & FLAGS_MASK) | PAGE_WRITE;
		vmm_protect_page(vas, page_aligned_addr, new_flags);
	}

pass:
	return;

fail:
	page_fault_fail(r);
}

[[noreturn]]
static void page_fault_fail(struct registers* r)
{
	// GDB BREAKPOINT
	uint64_t fault_addr;
	__asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));
	uint64_t cr3;
	__asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

	int present = !(r->err_code & 0x1);
	int rw = r->err_code & 0x2;
	int user = r->err_code & 0x4;
	int reserved = r->err_code & 0x8;
	int id = r->err_code & 0x10;

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
