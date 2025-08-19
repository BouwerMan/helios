/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <kernel/types.h>
#include <util/list.h>
#include <util/log.h>

/**
 * struct address_space - Represents a virtual address space.
 */
struct address_space {
	uptr pml4_phys;		  /* Physical address of the PML4 table. */
	struct list_head mr_list; /* List of memory regions. */
};

/**
 * struct memory_region - Represents a virtual memory area (VMA).
 */
struct memory_region {
	uptr start; /* VMR start, inclusive */
	uptr end;   /* VMR end, exclusive */

	unsigned long prot;  /* Memory protection flags. */
	unsigned long flags; /* Additional flags for the region. */

	struct address_space* owner; /* Owning address space. */
	struct list_head list; /* Links into the address_space's mr_list. */
};

/**
 * add_region - Adds a memory region to an address space.
 * @vas: The address space to add the region to.
 * @mr: The memory region to add.
 */
static inline void add_region(struct address_space* vas,
			      struct memory_region* mr)
{
	mr->owner = vas;
	list_add(&vas->mr_list, &mr->list);
}

/**
 * remove_region - Removes a memory region from its address space.
 * @mr: The memory region to remove.
 */
static inline void remove_region(struct memory_region* mr)
{
	list_remove(&mr->list);
}

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
 * address_space_dup - Duplicates an address space.
 * @dest: The destination address space.
 * @src: The source address space.
 *
 * Return: 0 on success, -1 on failure.
 */
int address_space_dup(struct address_space* dest, struct address_space* src);

/**
 * map_region - Creates and maps a new memory region.
 * @vas: The address space to add the new region to.
 * @start: The starting virtual address of the region.
 * @end: The ending virtual address of the region.
 * @prot: The memory protection flags for the region.
 * @flags: Additional flags for the region.
 *
 * This function is a simple wrapper that allocates a new memory_region
 * and immediately adds it to the specified address space.
 */
static inline void map_region(struct address_space* vas,
			      uptr start,
			      uptr end,
			      unsigned long prot,
			      unsigned long flags)
{
	log_debug(
		"Mapping region: start=0x%lx, end=0x%lx, prot=0x%lx, flags=0x%lx",
		start,
		end,
		prot,
		flags);
	struct memory_region* mr = alloc_mem_region(start, end, prot, flags);
	if (!mr) return;
	add_region(vas, mr);
}
