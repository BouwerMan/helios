/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <kernel/types.h>
#include <lib/list.h>
#include <mm/page_tables.h>

/**
 * struct address_space - Represents a virtual address space.
 */
struct address_space {
	uptr pml4_phys;		  /* Physical address of the PML4 table. */
	pgd_t* pml4;		  /* Has to go second for switch.asm */
	struct list_head mr_list; /* List of memory regions. */
};

/**
 * struct memory_region - Represents a virtual memory area (VMA).
 */
struct memory_region {
	uptr start;		     /* VMR start, inclusive */
	uptr end;		     /* VMR end, exclusive */

	unsigned long prot;	     /* Memory protection flags. */
	unsigned long flags;	     /* Additional flags for the region. */

	struct address_space* owner; /* Owning address space. */
	struct list_head list; /* Links into the address_space's mr_list. */
};

/**
 * These are both inline so that our page_fault handler doesn't take 1000 years
 */

static inline bool is_within_region(struct memory_region* mr, vaddr_t vaddr)
{
	return vaddr >= mr->start && vaddr < mr->end;
}

static inline bool is_within_vas(struct address_space* vas, vaddr_t vaddr)
{
	bool res = false;

	struct memory_region* pos = nullptr;
	list_for_each_entry (pos, &vas->mr_list, list) {
		res = is_within_region(pos, vaddr);
		if (res) return res;
	}

	return res;
}

struct memory_region* get_region(struct address_space* vas, vaddr_t vaddr);

/**
 * add_region - Adds a memory region to an address space.
 * @vas: The address space to add the region to.
 * @mr: The memory region to add.
 */
void add_region(struct address_space* vas, struct memory_region* mr);

/**
 * remove_region - Removes a memory region from its address space.
 * @mr: The memory region to remove.
 */
void remove_region(struct memory_region* mr);

/**
 * address_space_init - Initializes the address space management system.
 */
void address_space_init();

/**
 * alloc_mem_region - Allocates and initializes a new memory_region.
 * @start: The starting address of the memory region.
 * @end: The ending address of the memory region.
 * @prot: The protection flags for the memory region.
 * @flags: The flags for the memory region.
 *
 * Return: A pointer to the new memory_region, or NULL on failure.
 */
struct memory_region*
alloc_mem_region(uptr start, uptr end, unsigned long prot, unsigned long flags);

/**
 * destroy_mem_region - Destroys and deallocates a memory_region.
 * @mr: The region to destroy
 */
void destroy_mem_region(struct memory_region* mr);

/**
 * address_space_dup - Duplicates an address space.
 * @dest: The destination address space.
 * @src: The source address space.
 *
 * Return: 0 on success, -1 on failure.
 */
int address_space_dup(struct address_space* dest, struct address_space* src);

void unmap_region(struct address_space* vas, struct memory_region* mr);
void address_space_destroy(struct address_space* vas);

void vas_set_pml4(struct address_space* vas, pgd_t* pml4);

/**
 * map_region - Creates and maps a new memory region.
 * @vas: The address space to add the new region to.
 * @start: The starting virtual address of the region.
 * @end: The ending virtual address of the region.
 * @prot: The memory protection flags for the region.
 * @flags: Additional flags for the region.
 *
 * Return: 0 on success, negative error code on failure
 */
int map_region(struct address_space* vas,
	       uptr start,
	       uptr end,
	       unsigned long prot,
	       unsigned long flags);

void address_space_dump(struct address_space* vas);
