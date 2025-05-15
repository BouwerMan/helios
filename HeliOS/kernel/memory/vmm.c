/**
 * @file kernel/memory/vmm.c
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

#include <string.h>

#include <kernel/memory/pmm.h>
#include <kernel/memory/vmm.h>
#include <kernel/sys.h>
#include <limine.h>

#ifndef __VMM_DEBUG__
#define LOG_LEVEL 1
#define FORCE_LOG_REDEF
#include <util/log.h>
#undef FORCE_LOG_REDEF
#else
#include <util/log.h>
#endif

// stores size of exe mappings (usually just kernel)
static size_t exe_size = 0;
uint64_t* pml4 = NULL;

// Bitmap allocator stuff
static uint64_t* bitmap = NULL;
static size_t bitmap_size = 0; ///< Size of the bitmap in terms of uint64_t entries.
static size_t total_page_count = 0;
static size_t free_page_count = 0;

static uint64_t g_hhdm_offset;
#define PHYS_TO_VIRT(p) ((void*)((uintptr_t)(p) + g_hhdm_offset))
#define VIRT_TO_PHYS(v) ((uintptr_t)(v) - g_hhdm_offset)

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
	if (__builtin_expect((pml4_phys_addr & 0xFFF) != 0, 0)) {
		panic("CR3 address not 4 KiB aligned");
	}

	__asm__ volatile("mov %0, %%cr3" ::"r"(pml4_phys_addr) : "memory");
}

/**
 * @brief Maps a memory map entry into the virtual memory space.
 *
 * This function maps a memory region described by a Limine memory map entry
 * into the virtual memory space. It also handles special cases for mapping
 * executable and module sections to their designated virtual addresses.
 *
 * @param entry Pointer to the Limine memory map entry to be mapped.
 * @param exe Pointer to the Limine executable address response structure.
 * @param hhdm_offset Offset for the Higher Half Direct Map (HHDM).
 */
static void map_memmap_entry(struct limine_memmap_entry* entry, struct limine_executable_address_response* exe,
			     uint64_t hhdm_offset)
{
	for (uint64_t addr = entry->base; addr < entry->base + entry->length; addr += PAGE_SIZE) {
		vmm_map((void*)(addr + hhdm_offset), (void*)addr, PAGE_PRESENT | PAGE_WRITE);
		// We have to manually map the kernel section, so we map it a second time to exe->virtual_base
		if (entry->type != LIMINE_MEMMAP_EXECUTABLE_AND_MODULES) continue;
		uint64_t exe_virt = (addr - entry->base) + exe->virtual_base;
		vmm_map((void*)exe_virt, (void*)addr, PAGE_PRESENT | PAGE_WRITE);
		exe_size += PAGE_SIZE;
	}
}

/**
 * @brief Checks if a given memory map entry is valid for use.
 *
 * @param entry A pointer to the memory map entry to validate.
 * @return true if the entry is not usable or reserved; false otherwise.
 */
static bool is_valid_entry(struct limine_memmap_entry* entry)
{
	return entry->type == LIMINE_MEMMAP_USABLE || entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
	       entry->type == LIMINE_MEMMAP_EXECUTABLE_AND_MODULES || entry->type == LIMINE_MEMMAP_FRAMEBUFFER;
}

static bool ranges_overlap(uint64_t a_start, uint64_t a_end, uint64_t b_start, uint64_t b_end)
{
	return !(a_end <= b_start || a_start >= b_end);
}

/**
 * @brief Initializes the Virtual Memory Manager (VMM).
 *
 * This function sets up the initial page tables, including recursive mapping,
 * identity mapping for low memory, and mapping the Higher Half Direct Map (HHDM).
 *
 * @param mmap Pointer to the Limine memory map response structure.
 * @param exe Pointer to the Limine executable address response structure.
 * @param hhdm_offset Offset for the Higher Half Direct Map (HHDM).
 */
void vmm_init(struct limine_memmap_response* mmap, struct limine_executable_address_response* exe, uint64_t hhdm_offset)
{
	g_hhdm_offset = hhdm_offset;
	log_debug("hhdm_offset: %lx", g_hhdm_offset);
	uint64_t* pml4_phys = pmm_alloc_page();
	pml4 = PHYS_TO_VIRT(pml4_phys);
	log_debug("pml4_phys: %lx, pml4_virt: %lx", (uint64_t)pml4_phys, (uint64_t)pml4);
	if (pml4 == NULL) {
		log_error("VMM Initialization failed. PMM didn't return a valid page");
		panic("VMM Initialization failed.");
	}
	log_debug("pml4 address: %p", pml4);
	memset(pml4, 0, PAGE_SIZE); // Clear page tables

	// Recursive mapping
	// TODO: Actually use this once CR3 is set
	pml4[510] = VIRT_TO_PHYS(pml4) | PAGE_PRESENT | PAGE_WRITE;

	// Identity map first 64 MiB, skip first 1 MiB
	for (uint64_t vaddr = 0x100000; vaddr < LOW_IDENTITY; vaddr += PAGE_SIZE) {
		vmm_map((void*)vaddr, (void*)vaddr, PAGE_PRESENT | PAGE_WRITE);
	}

	// Map HHDM
	for (size_t i = 0; i < mmap->entry_count; i++) {
		struct limine_memmap_entry* entry = mmap->entries[i];
		// Skip any that are not part of the limine hhdm mapping
		if (!is_valid_entry(entry)) continue;
		map_memmap_entry(entry, exe, hhdm_offset);
	}

	// Load the PML4 physical address into CR3 to activate the page tables
	vmm_load_cr3((uintptr_t)pml4_phys);

	// Setting up heap
	log_debug("Found exe size to be: %lx", exe_size);
	uint64_t kernel_start = exe->virtual_base;
	uint64_t kernel_end = kernel_start + exe_size;
	log_info("Kernel range: [%lx - %lx)", kernel_start, kernel_end);
	log_info("Heap range:   [%lx - %lx)", KERNEL_HEAP_BASE, KERNEL_HEAP_LIMIT);
	log_info("Recursive slot: [%lx - %lx)", 0xFFFFFE0000000000, 0xFFFFFEFFFFFFFFFF);
	if (ranges_overlap(kernel_start, kernel_end, KERNEL_HEAP_BASE, KERNEL_HEAP_LIMIT)) {
		panic("KERNEL AND KERNEL HEAP OVERLAP");
	}

	size_t bitmap_size_bytes = (KERNEL_HEAP_LIMIT - KERNEL_HEAP_BASE) / PAGE_SIZE / UINT8_WIDTH;
	bitmap_size = bitmap_size_bytes / sizeof(uintptr_t);
	total_page_count = bitmap_size * BITSET_WIDTH;
	log_debug("Bitmap size bytes: %zu, total pages: %zu", bitmap_size_bytes, total_page_count);
	// NOTE: Initially setting up kernel heap as bump allocator, can move to something more complex
	// if needed (reuse of pages)
	size_t req_pages = CEIL_DIV(bitmap_size_bytes, PAGE_SIZE);
	log_debug("Allocating %zu pages for vmm bitmap", req_pages);
	bitmap = PHYS_TO_VIRT(pmm_alloc_contiguous(req_pages));
	if (!bitmap) panic("Could not alloc vmm bitmap");
	log_debug("Putting bitmap at location: 0x%lx", (uint64_t)bitmap);
	// Initialize the bitmap: Mark all pages as free (heap starts empty).
	memset(bitmap, 0x00, bitmap_size_bytes);
	free_page_count = total_page_count;
}

// Going to go ahead and use a bitmap allocator like a real man

/**
 * @brief Allocates a contiguous range of pages in virtual memory.
 *
 * This function searches for a contiguous range of free pages in the bitmap
 * and allocates them. It maps the allocated virtual pages to physical pages
 * using the page table hierarchy. If no suitable range is found, the function
 * returns `NULL`.
 *
 * @param count The number of contiguous pages to allocate.
 * @return A pointer to the starting virtual address of the allocated pages,
 *         or `NULL` if allocation fails.
 *
 * @note The function assumes that the bitmap and page table structures are
 *       properly initialized before calling.
 */
void* vmm_alloc_pages(size_t count, bool contiguous)
{
	if (count == 0) {
		log_warn("count cannot be 0");
		return NULL;
	}
	log_debug("Asked to allocate %zu pages", count);
	size_t cont_start = SIZE_MAX;
	size_t cont_len = 0;
	for (size_t i = 0; i < total_page_count; i++) {
		uint64_t word_offset = i / BITSET_WIDTH;
		uint64_t bit_offset = i % BITSET_WIDTH;
		bool is_free = (bitmap[word_offset] & (1ULL << bit_offset)) == 0;

		if (is_free) {
			if (cont_start == SIZE_MAX) cont_start = i;

			bool valid_len = (++cont_len) >= count;
			if (valid_len && !contiguous) goto allocate_pages;
			if (valid_len && contiguous) goto allocate_pages_contiguous;
		} else {
			cont_start = SIZE_MAX;
			cont_len = 0;
		}
	}
	log_warn("No valid contiguous range found for %zu pages", count);
	return NULL;

allocate_pages:
	// Allocate contiguous in virtual memory, not necissarily in physical memory
	for (size_t i = cont_start; i < cont_start + count; i++) {
		// Calculate bitmap offsets
		uint64_t word_offset = i / BITSET_WIDTH;
		uint64_t bit_offset = i % BITSET_WIDTH;

		// Setting page as used
		bitmap[word_offset] |= (1ULL << bit_offset);

		uint64_t virt_addr = KERNEL_HEAP_BASE + (i * PAGE_SIZE);
		void* phys_addr = pmm_alloc_page();
		log_debug("Allocating page at phys %p, and virt %lx", phys_addr, virt_addr);
		vmm_map((void*)virt_addr, phys_addr, PAGE_PRESENT | PAGE_WRITE);
	}
	free_page_count -= count;
	return (void*)(KERNEL_HEAP_BASE + (cont_start * PAGE_SIZE));

allocate_pages_contiguous:
	void* phys_addr = pmm_alloc_contiguous(count);
	uint64_t start_virt_addr = KERNEL_HEAP_BASE + (cont_start * PAGE_SIZE);
	log_debug("Allocating %zu contiguous pages, starting at phys %p, and virt %lx", count, phys_addr,
		  start_virt_addr);

	for (size_t virt_i = cont_start, phys_i = 0; virt_i < cont_start + count; virt_i++, phys_i++) {
		// Calculate bitmap offsets
		uint64_t word_offset = virt_i / BITSET_WIDTH;
		uint64_t bit_offset = virt_i % BITSET_WIDTH;

		// Setting page as used
		bitmap[word_offset] |= (1ULL << bit_offset);

		uint64_t virt_addr = KERNEL_HEAP_BASE + (virt_i * PAGE_SIZE);
		size_t phys_i = virt_i - cont_start;
		uint64_t map_phys = (uintptr_t)phys_addr + (phys_i * PAGE_SIZE);

		vmm_map((void*)virt_addr, (void*)map_phys, PAGE_PRESENT | PAGE_WRITE);
	}
	free_page_count -= count;
	return (void*)(KERNEL_HEAP_BASE + (cont_start * PAGE_SIZE));
}

/**
 * @brief Frees a range of pages in the virtual memory manager.
 *
 * This function marks a range of pages as free in the bitmap and unmaps them
 * from the virtual memory space. It ensures that the pages being freed are
 * within bounds and logs an error if an out-of-bounds address is provided.
 *
 * @param addr The starting virtual address of the pages to free.
 * @param count The number of pages to free.
 *
 * @note The function assumes that the address provided is aligned to the page
 *       size and that the count does not exceed the total number of pages.
 */
void vmm_free_pages(void* addr, size_t count)
{
	uint64_t page_index = ((uint64_t)addr - KERNEL_HEAP_BASE) / PAGE_SIZE;
	if (page_index >= total_page_count) {
		log_error("Attempted to free page out of bounds: %lu", page_index);
		return;
	}
	log_debug("Freeing index %ld", page_index);
	uint64_t end_index = page_index + count;
	for (; page_index < end_index; page_index++) {
		uint64_t word_offset = page_index / BITSET_WIDTH;
		uint64_t bit_offset = page_index % BITSET_WIDTH;
		bitmap[word_offset] &= ~(1ULL << bit_offset);
		vmm_unmap((void*)(KERNEL_HEAP_BASE + (page_index * PAGE_SIZE)), true);
	}
	free_page_count += count;
}

/**
 * @brief Invalidates a single page in the TLB (Translation Lookaside Buffer).
 *
 * @param vaddr The virtual address of the page to invalidate.
 */
static inline void invalidate(void* vaddr)
{
	__asm__ volatile("invlpg (%0)" ::"r"(vaddr) : "memory");
}

/**
 * @brief Allocates and initializes a new page table.
 *
 * @return The physical address of the newly allocated page table.
 */
static uint64_t vmm_alloc_table()
{
	void* new_page = PHYS_TO_VIRT(pmm_alloc_page());
	memset(new_page, 0, PAGE_SIZE);
	return VIRT_TO_PHYS((uint64_t)new_page);
}

#define VADDR_PML4_INDEX(vaddr) (((uint64_t)(vaddr) >> 39) & 0x1FF)
#define VADDR_PDPT_INDEX(vaddr) (((uint64_t)(vaddr) >> 30) & 0x1FF)
#define VADDR_PD_INDEX(vaddr)	(((uint64_t)(vaddr) >> 21) & 0x1FF)
#define VADDR_PT_INDEX(vaddr)	(((uint64_t)(vaddr) >> 12) & 0x1FF)

/**
 * @brief Maps a virtual address to a physical address in the page tables.
 *
 * This function ensures that all required page table levels (PML4, PDPT, PD, PT)
 * are present and properly initialized. If a level is missing, it allocates a new
 * page for it and initializes it to zero. Finally, it maps the given virtual address
 * to the specified physical address with the provided flags.
 *
 * @param virt_addr The virtual address to map.
 * @param phys_addr The physical address to map to.
 * @param flags The flags to set for the mapping (e.g., PAGE_PRESENT, PAGE_WRITE).
 */
void vmm_map(void* virt_addr, void* phys_addr, uint64_t flags)
{
	// log_debug("Mapping virt_addr: %p, to phys_addr: %p", virt_addr,
	// 	  phys_addr);
	flags |= PAGE_PRESENT | PAGE_WRITE;
	uint64_t pml4_i = VADDR_PML4_INDEX(virt_addr);
	if ((pml4[pml4_i] & PAGE_PRESENT) == 0) {
		pml4[pml4_i] = vmm_alloc_table() | flags;
	}

	// Extract the physical address and convert it to a usable virtual pointer
	uint64_t* pdpt = (uint64_t*)PHYS_TO_VIRT(pml4[pml4_i] & ~FLAGS_MASK); // Mask off flags
	uint64_t pdpt_i = VADDR_PDPT_INDEX(virt_addr);
	if ((pdpt[pdpt_i] & PAGE_PRESENT) == 0) {
		pdpt[pdpt_i] = vmm_alloc_table() | flags;
	}

	uint64_t* pd = (uint64_t*)PHYS_TO_VIRT(pdpt[pdpt_i] & ~FLAGS_MASK); // Mask off flags
	uint64_t pd_i = VADDR_PD_INDEX(virt_addr);
	if ((pd[pd_i] & PAGE_PRESENT) == 0) {
		pd[pd_i] = vmm_alloc_table() | flags;
	}

	// Mask off flags
	uint64_t* pt = (uint64_t*)PHYS_TO_VIRT(pd[pd_i] & ~FLAGS_MASK);
	uint64_t pt_i = VADDR_PT_INDEX(virt_addr);
	if ((pt[pt_i] & PAGE_PRESENT) == 0) {
		pt[pt_i] = (uint64_t)phys_addr | flags;
	} else {
		log_warn("Tried to map existing entry, virt_addr: 0x%lx, phys_addr: 0x%lx", (uint64_t)virt_addr,
			 (uint64_t)phys_addr);
		invalidate(virt_addr);
		pt[pt_i] = (uint64_t)phys_addr | flags;
	}
}

void* vmm_translate(void* virt_addr)
{
	uint64_t va = (uint64_t)virt_addr;
	log_debug("Translating virtual address %lx", va);

	uint64_t pml4_i = VADDR_PML4_INDEX(va);
	if (!(pml4[pml4_i] & PAGE_PRESENT)) return NULL;

	uint64_t* pdpt = (uint64_t*)PHYS_TO_VIRT(pml4[pml4_i] & PAGE_FRAME_MASK);
	log_debug("pml4: %p, pml4_i: %lu, pdpt: %p", (void*)pml4, pml4_i, (void*)pdpt);
	uint64_t pdpt_i = VADDR_PDPT_INDEX(va);
	if (!(pdpt[pdpt_i] & PAGE_PRESENT)) return NULL;

	uint64_t* pd = (uint64_t*)PHYS_TO_VIRT(pdpt[pdpt_i] & PAGE_FRAME_MASK);
	log_debug("pdpt: %p, pdpt_i: %lu, pd: %p", (void*)pdpt, pdpt_i, (void*)pd);
	uint64_t pd_i = VADDR_PD_INDEX(va);
	if (!(pd[pd_i] & PAGE_PRESENT)) return NULL;

	uint64_t* pt = (uint64_t*)PHYS_TO_VIRT(pd[pd_i] & PAGE_FRAME_MASK);
	log_debug("pd: %p, pd_i: %lu, pt: %p", (void*)pt, pd_i, (void*)pt);
	uint64_t pt_i = VADDR_PT_INDEX(va);
	if (!(pt[pt_i] & PAGE_PRESENT)) return NULL;

	uint64_t frame = pt[pt_i] & PAGE_FRAME_MASK;
	uint64_t offset = va & 0xFFF;

	log_debug("pt[pt_i]: %lx, pt_i: %lu, frame: %lx, offset: %lx", pt[pt_i], pt_i, frame, offset);

	log_debug("Translating %p, got %p as phys address", virt_addr, (void*)(frame + offset));
	return (void*)(frame + offset);
}

/**
 * @brief Unmaps a virtual address from the page tables.
 *
 * This function removes the mapping for a given virtual address by traversing
 * the page table hierarchy. If the `free_phys` parameter is set to true, the
 * corresponding physical page is also freed. It ensures that the TLB is
 * invalidated for the unmapped address to prevent stale entries.
 *
 * @param virt_addr The virtual address to unmap.
 * @param free_phys If true, the physical page associated with the virtual
 *                  address is freed.
 *
 * @note If any level of the page table hierarchy is not present, an error
 *       is logged, and the function exits without making changes.
 */
void vmm_unmap(void* virt_addr, bool free_phys)
{
	// TODO: Detect and free empty tables
	uint64_t va = (uint64_t)virt_addr;

	uint64_t pml4_i = VADDR_PML4_INDEX(va);
	if (!(pml4[pml4_i] & PAGE_PRESENT)) goto not_present;

	uint64_t* pdpt = (uint64_t*)PHYS_TO_VIRT(pml4[pml4_i] & PAGE_FRAME_MASK);
	uint64_t pdpt_i = VADDR_PDPT_INDEX(va);
	if (!(pdpt[pdpt_i] & PAGE_PRESENT)) goto not_present;

	uint64_t* pd = (uint64_t*)PHYS_TO_VIRT(pdpt[pdpt_i] & PAGE_FRAME_MASK);
	uint64_t pd_i = VADDR_PD_INDEX(va);
	if (!(pd[pd_i] & PAGE_PRESENT)) goto not_present;

	uint64_t* pt = (uint64_t*)PHYS_TO_VIRT(pd[pd_i] & PAGE_FRAME_MASK);
	uint64_t pt_i = VADDR_PT_INDEX(va);
	if (!(pt[pt_i] & PAGE_PRESENT)) goto not_present;
	if (free_phys) {
		uint64_t phys_addr = pt[pt_i] & PAGE_FRAME_MASK;
		pmm_free_page((void*)phys_addr);
	}
	pt[pt_i] = 0;

	invalidate(virt_addr);
	return;

not_present:
	log_error("Couldn't traverse tables for 0x%lx, something in the chain was already marked not present",
		  (uint64_t)virt_addr);
}
