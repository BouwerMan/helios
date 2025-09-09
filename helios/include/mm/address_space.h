/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "kernel/types.h"
#include "lib/list.h"
#include "mm/page.h"
#include "mm/page_tables.h"

/**
 * struct address_space - Represents a virtual address space.
 *
 * Invariants:
 *  - mr_list contains non-overlapping regions sorted by start (recommended).
 *  - All regions are page-aligned: start % PAGE_SIZE == 0, end % PAGE_SIZE == 0.
 */
struct address_space {
	uptr pml4_phys;		  /* Physical address of the PML4 table. */
	pgd_t* pml4;		  /* Has to go second for switch.asm */
	struct list_head mr_list; /* List of memory regions (VMAs). */
};

/**
 * enum mr_kind - Backing type of a memory_region.
 *
 * MR_FILE: pages are faulted by reading from a file (vfs_inode).
 * MR_ANON: pages are zero-filled on demand (no file IO).
 * MR_DEVICE: (future) MMIO or special pager; not needed for ELF, but handy long-term.
 */
enum mr_kind {
	MR_ANON = 0,
	MR_FILE = 1,
	MR_DEVICE = 2,
};

/**
 * struct mr_file - File-backed bookkeeping for demand paging.
 *
 * file_lo: page-aligned file offset that corresponds to 'start' (vstart).
 * file_hi: exclusive end of the initialized bytes for THIS segment
 *          (i.e., p_offset + p_filesz). Never read past this; zero the rest.
 * pgoff   : file_lo / PAGE_SIZE, convenient for a page cache index.
 * delta   : intra-page bias: p_vaddr % PAGE == p_offset % PAGE (0..PAGE_SIZE-1).
 *           Not strictly required if you always use file_lo/start deltas, but useful
 *           for debugging and certain corner cases.
 */
struct mr_file {
	struct vfs_inode* inode;
	off_t file_lo;	/* aligned_down(p_offset) */
	off_t file_hi;	/* p_offset + p_filesz (exclusive) */
	pgoff_t pgoff;	/* file_lo >> PAGE_SHIFT */
	uint16_t delta; /* p_vaddr - align_down(p_vaddr) */
};

/**
 * struct mr_anon - Anonymous (zero-fill) bookkeeping.
 * tag: optional accounting/debug identifier (e.g., "bss", "heap").
 */
struct mr_anon {
	uint32_t tag;
};

/**
 * struct memory_region - Represents a virtual memory area (VMA).
 *
 * Semantics for ELF segments:
 *  - FILE region covers [vstart, vstart + align_up(delta + p_filesz)).
 *    Reads are clamped to [file_lo, file_hi) and any unread tail is zeroed.
 *  - If p_memsz > p_filesz, a second ANON region covers the remainder:
 *      [align_up(p_vaddr + p_filesz), align_up(p_vaddr + p_memsz)).
 *
 * Fault-time algorithm (FILE):
 *  let VA = align_down(fault_addr)
 *  page_off = VA - start
 *  file_off = file.file_lo + page_off
 *  init_left = file.file_hi - file_off
 *  to_read = init_left > 0 ? min(PAGE_SIZE, (size_t)init_left) : 0
 *  read 'to_read' bytes; zero the rest of the page
 *
 * Fault-time algorithm (ANON):
 *  allocate zeroed page; map with 'prot'
 */
struct memory_region {
	uptr start;	     /* VMA start, inclusive (page-aligned) */
	uptr end;	     /* VMA end, exclusive (page-aligned) */

	unsigned long prot;  /* PROT_READ/WRITE/EXEC (ELF p_flags->prot). */
	unsigned long flags; /* MAP_PRIVATE/SHARED and your VM bits. */

	enum mr_kind kind;   /* MR_FILE vs MR_ANON (MR_DEVICE optional). */
	bool is_private;     /* True for MAP_PRIVATE → CoW on first write. */

	union {
		struct mr_file file; /* Valid when kind == MR_FILE */
		struct mr_anon anon; /* Valid when kind == MR_ANON */
	};

	struct address_space* owner; /* Owning address space. */
	struct list_head list;	     /* Link in address_space::mr_list. */
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
 *
 * FIXME:
 * @vas: The address space to add the new region to.
 * @inode: The inode backing the region, or NULL for anonymous.
 * @file_offset: The offset within the file to start mapping from.
 * @start: The starting virtual address of the region.
 * @end: The ending virtual address of the region.
 * @prot: The memory protection flags for the region.
 * @flags: Additional flags for the region.
 *
 * Return: 0 on success, negative error code on failure
 */
int map_region(struct address_space* vas,
	       struct mr_file file,
	       uptr start,
	       uptr end,
	       unsigned long prot,
	       unsigned long flags);

void address_space_dump(struct address_space* vas);
struct address_space* alloc_address_space();
