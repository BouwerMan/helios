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

/** 
 * For this VMM, I will be doing identity mapping like a man.
 * To do this, I will basically just copy limine's hhdm, that is how I will get the memory for page tables.
 * Once that is setup, I have a "heap" for general memory allocation.
 * The slab allocator and any other special purpose allocators should get pages directly.
 */

#include <kernel/spinlock.h>
#include <stdint.h>
#include <string.h>

#include <kernel/kmath.h>
#include <kernel/memory/memory_limits.h>
#include <kernel/memory/pmm.h>
#include <kernel/memory/vmm.h>
#include <kernel/sys.h>
#include <limine.h>

#ifndef __VMM_DEBUG__
#undef LOG_LEVEL
#define LOG_LEVEL 0
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

// Big boy array is a binary tree in a breadth-first array layout
static struct buddy_block blocks[BUDDY_NODES] = { 0 };
static struct buddy_allocator alr = { 0 };

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

#define LEFT_CHILD(node)  (2 * node + 1)
#define RIGHT_CHILD(node) (2 * node + 2)
#define PARENT(node)	  ((node - 1) / 2)

static inline uintptr_t index_to_addr(size_t index, uintptr_t base, size_t order)
{
	size_t level = MAX_ORDER - order;
	size_t level_start = (1UL << level) - 1;
	size_t offset = index - level_start;
	return base + (offset << order);
}

static inline uintptr_t addr_to_index(uintptr_t addr, uintptr_t base, size_t order)
{
	size_t level = MAX_ORDER - order;
	size_t offset = (addr - base) >> order;
	size_t level_start = (1UL << level) - 1;
	return level_start + offset;
}

static void insert_block(struct buddy_allocator* allocator, uintptr_t addr, uint8_t order)
{
	log_debug("Inserting block addr: %lx, order: %u", addr, order);
	size_t block_index = addr_to_index(addr, allocator->base, order);
	struct buddy_block* block = &blocks[block_index];
	list_append(&allocator->free_lists[order], &block->link);
	block->order = order;
	block->state = BLOCK_FREE;
}

// TODO: Make allocator agnostic
static inline size_t get_buddy_idx(size_t index, size_t order)
{
	int level = MAX_ORDER - order;
	size_t level_start = (1 << level) - 1;
	size_t offset_in_level = index - level_start;

	// Flip the lowest bit to get the buddy's offset
	size_t buddy_offset = offset_in_level ^ 1;
	return level_start + buddy_offset;
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
	log_debug("pml4 address: %p", (void*)pml4);
	memset(pml4, 0, PAGE_SIZE); // Clear page tables

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

	// Time to setup buddy

	// Setting up heap
	log_debug("Found exe size to be: %lx", exe_size);
	uint64_t kernel_start = exe->virtual_base;
	uint64_t kernel_end = kernel_start + exe_size;
	log_info("Kernel range: [%lx - %lx)", kernel_start, kernel_end);
	log_info("Heap range:   [%llx - %llx)", KERNEL_HEAP_BASE, KERNEL_HEAP_LIMIT);
	log_info("heap+order: %lx", KERNEL_HEAP_BASE + (1 << MAX_ORDER));
	if (ranges_overlap(kernel_start, kernel_end, KERNEL_HEAP_BASE, KERNEL_HEAP_LIMIT)) {
		panic("KERNEL AND KERNEL HEAP OVERLAP");
	}

	// TODO: Get rid of lists below MIN_ORDER
	for (size_t i = MIN_ORDER; i <= MAX_ORDER; i++) {
		list_init(&alr.free_lists[i]);
	}
	uintptr_t addr = KERNEL_HEAP_BASE;
	size_t remaining = KERNEL_POOL;

	alr.base = KERNEL_HEAP_BASE;
	alr.size = KERNEL_POOL;
	spinlock_init(&alr.lock);

	spinlock_acquire(&alr.lock);

	while (remaining > 0) {
		int order = MAX_ORDER;

		// Find largest block that fits and is aligned
		while ((1UL << order) > remaining || (addr & ((1UL << order) - 1)) != 0) {
			order--;
		}

		// Insert this block into the free list for that order
		insert_block(&alr, addr, order);

		addr += (1UL << order);
		remaining -= (1UL << order);
	}
	spinlock_release(&alr.lock);
}

static struct buddy_block* split_until_order(struct buddy_allocator* allocator, uintptr_t addr, int current_order,
					     int target_order)
{
	size_t block_index = addr_to_index(addr, allocator->base, current_order);
	struct buddy_block* block = &blocks[block_index];
	if (current_order == target_order) {
		block->state = BLOCK_ALLOCATED;
		return block;
	}

	// Now we split into 2 blocks
	struct buddy_block* parent = block;
	struct buddy_block* left = &blocks[LEFT_CHILD(block_index)];
	struct buddy_block* right = &blocks[RIGHT_CHILD(block_index)];

	// These are always set this way
	parent->state = BLOCK_SPLIT;
	parent->order = current_order;
	right->state = BLOCK_FREE;
	right->order = parent->order - 1;
	left->order = parent->order - 1;
	left->state = BLOCK_SPLIT;

	// Add right to free list
	list_append(&allocator->free_lists[right->order], &right->link);

	size_t size = 1 << (current_order - 1);
	uintptr_t left_addr = addr;
	uintptr_t right_addr = addr + size;
	log_debug("block_index: %zu, left child: %zu (%lx), right child: %zu (%lx)", block_index,
		  LEFT_CHILD(block_index), left_addr, RIGHT_CHILD(block_index), right_addr);

	// We always recurse with the left child
	return split_until_order(allocator, left_addr, left->order, target_order);
}

// Size is in bytes
static uintptr_t buddy_alloc(struct buddy_allocator* allocator, size_t size)
{
	size_t rounded_size = round_to_power_of_2(size);
	int target_order = log2(rounded_size);
	log_debug("Allocating rounded_size: %lx, target_order: %d", rounded_size, target_order);

	for (size_t i = target_order; i <= MAX_ORDER; i++) {
		log_debug("Checking order: %zu", i);
		struct list* order_list = &allocator->free_lists[i];
		if (list_empty(order_list)) continue;
		log_debug("List not empty for order: %zu", i);

		// Getting next entry in list
		struct buddy_block* block;
		list_for_each_entry(block, order_list, link)
		{
			if (block->state == BLOCK_FREE) {
				break;
			} else {
				// Since everything in this list should be free, going to go ahead and remove it
				log_debug(
					"Found non free block in free list with order: %zu, blockmeta_order: %u, blockmeta_state: %u",
					i, block->order, block->state);
				list_remove(&block->link);
			}
		}

		// Just 1 final sanity check
		if (block->state != BLOCK_FREE) continue;

		// Remove it from the list and mark it as split if needed
		list_remove(&block->link);
		if (block->order > target_order) block->state = BLOCK_SPLIT;

		size_t block_index = block - blocks;
		uintptr_t addr = index_to_addr(block_index, allocator->base, block->order);
		log_debug("block_index: %zu, addr: %lx", block_index, addr);

		// Now we split recursively until we get the right size
		struct buddy_block* split_block = split_until_order(allocator, addr, block->order, target_order);
		size_t split_block_index = split_block - blocks;
		uintptr_t split_addr = index_to_addr(split_block_index, allocator->base, split_block->order);
		log_debug("Found block at block_index: %zu, addr: %lx, with order: %u", split_block_index, split_addr,
			  split_block->order);
		return split_addr;
	}

	return 0;
}

void* valloc(size_t pages, size_t flags)
{
	size_t rounded_pages = round_to_power_of_2(pages);

	// TODO: FIGURE OUT MEMORY ZONES
	if (flags & ALLOC_KERNEL || flags & ALLOC_KDMA32) {
		spinlock_acquire(&alr.lock);
		uintptr_t vaddr_start = buddy_alloc(&alr, rounded_pages * PAGE_SIZE);
		if (vaddr_start == 0) {
			spinlock_release(&alr.lock);
			return NULL;
		}
		log_debug("Start of region at %lx", vaddr_start);
		for (size_t i = 0; i < rounded_pages; i++) {
			uintptr_t vaddr = vaddr_start + (i * PAGE_SIZE);
			uintptr_t paddr = (uintptr_t)pmm_alloc_page();

			log_debug("Mapping virtual address: %lx, to physical: %lx", vaddr, paddr);

			vmm_map((void*)vaddr, (void*)paddr, PAGE_PRESENT | PAGE_WRITE);
		}
		spinlock_release(&alr.lock);
		return (void*)vaddr_start;
	}
	return NULL;
}

static uint8_t get_order(struct buddy_allocator* allocator, uintptr_t addr)
{
	for (int order = MIN_ORDER; order <= MAX_ORDER; order++) {
		size_t index = addr_to_index(addr, allocator->base, order);

		if (index_to_addr(index, allocator->base, order) == addr && blocks[index].state == BLOCK_ALLOCATED) {
			return order;
		}
	}
	return UINT8_MAX;
}

static void combine_blocks(struct buddy_allocator* allocator, uintptr_t addr, int order)
{
	// size_t rounded_size = round_to_power_of_2(size);
	// int order = log2(rounded_size);
	// log_debug("addr %lx, order %d", addr, order);

	// size_t block_idx = BLOCK_TO_INDEX(addr, allocator->base, order);

	// Get the block and mark it as free
	size_t block_idx = addr_to_index(addr, allocator->base, order);
	log_debug("Found block at addr %lx with idx: %zu, current order %u", addr, block_idx, order);
	struct buddy_block* block = &blocks[block_idx];
	block->state = BLOCK_FREE;
	list_append(&allocator->free_lists[block->order], &block->link);

	// If we are already at the highest order we have freed everything
	if (order >= MAX_ORDER) return;

	size_t buddy_idx = get_buddy_idx(block_idx, block->order);

	struct buddy_block* buddy = &blocks[buddy_idx];
	log_debug("Found buddy with idx: %zu", buddy_idx);
	bool can_coalesce = buddy->state == BLOCK_FREE && buddy->order == block->order;

	if (can_coalesce) {
		log_debug("Coalescing block idx %zu with buddy idx %zu", block_idx, buddy_idx);
		// Remove from free lists and mark as invalid
		list_remove(&block->link);
		list_remove(&buddy->link);
		block->state = BLOCK_INVALID;
		buddy->state = BLOCK_INVALID;
		size_t parent_idx = PARENT(block_idx);
		struct buddy_block* parent = &blocks[parent_idx];

		// Only need to setup the order, the first stage of this function will handle the rest during recursion
		parent->order = block->order + 1;

		uintptr_t parent_addr = index_to_addr(parent_idx, allocator->base, parent->order);
		log_debug("parent addr: %lx, parent idx: %zu, parent order: %u (size=%zu)", parent_addr, parent_idx,
			  parent->order, 1UL << (size_t)parent->order);
		combine_blocks(allocator, parent_addr, parent->order);
	}
}

static void buddy_free(struct buddy_allocator* allocator, uintptr_t addr, size_t size)
{
	if (addr < KERNEL_HEAP_BASE || addr > KERNEL_HEAP_LIMIT) {
		log_warn("Invalid address specified: %lx is outside of heap range", addr);
		return;
	}

	spinlock_acquire(&alr.lock);
	uint8_t order = get_order(allocator, addr);
	if (order == UINT8_MAX) {
		log_error("Unable to determine order for address: %lx", addr);
		spinlock_release(&alr.lock);
		return;
	}
	// combine_blocks marks things as free if needed (including addr block)
	combine_blocks(allocator, addr, order);

	size_t block_size = 1UL << order;
	size_t pages = block_size / PAGE_SIZE;

	log_debug("Start of region to unmap and free at %lx", addr);
	for (size_t i = 0; i < pages; i++) {
		uintptr_t vaddr = addr + (i * PAGE_SIZE);
		vmm_unmap((void*)vaddr, true);
	}
	spinlock_release(&alr.lock);
}

void vfree_pages(void* addr, size_t pages)
{
	size_t size = pages * PAGE_SIZE;
	log_debug("Deallocating addr: %p, with size of %zu pages", addr, size);
	// TODO: figure out which allocator to use
	buddy_free(&alr, (uintptr_t)addr, size);
}

void vfree(void* addr)
{
	// This is a little slow, but thats what happens when you just vfree without telling me how many pages
	size_t size = 1 << get_order(&alr, (uintptr_t)addr);
	// TODO: figure out which allocator to use
	buddy_free(&alr, (uintptr_t)addr, size);
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
