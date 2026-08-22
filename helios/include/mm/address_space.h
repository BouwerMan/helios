/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "kernel/semaphores.h"
#include "kernel/types.h"
#include "lib/list.h"
#include "mm/page.h"
#include "mm/page_tables.h"

/**
 * @addtogroup mm
 * @{
 */

/**
 * @brief Represents a virtual address space.
 *
 * mr_list holds non-overlapping regions, sorted by start. Every region
 * is page-aligned: start and end are both multiples of PAGE_SIZE.
 */
struct address_space {
	uptr pml4_phys; /**< Physical address of the PML4 table. */
	pgd_t* pml4; /**< Virtual address of the PML4 table. Must stay second; switch.asm reads this field directly. */
	rwsem_t vma_lock;	  /**< Locks mr_list. */
	spinlock_t pgt_lock;	  /**< Locks page table modifications. */
	struct list_head mr_list; /**< List of memory regions (VMAs). */
};

/**
 * @brief Backing type of a memory_region.
 */
enum mr_kind {
	MR_ANON = 0,   /**< Pages are zero-filled on demand. No file I/O. */
	MR_FILE = 1,   /**< Pages are faulted in by reading from a file. */
	MR_DEVICE = 2, /**< Pages map MMIO or another special pager. Reserved for future use. */
};

/**
 * @brief File-backed bookkeeping for demand paging.
 */
struct mr_file {
	struct vfs_inode* inode; /**< The backing file. */
	off_t file_lo;		 /**< Page-aligned file offset that corresponds to the region's start. */

	/**
	 * @brief Exclusive end of the initialized bytes for this segment
	 * (p_offset + p_filesz).
	 *
	 * Never read past this offset. Zero the rest of the page instead.
	 */
	off_t file_hi;
	pgoff_t pgoff;	/**< file_lo expressed as a page index (file_lo >> PAGE_SHIFT). */
	uint16_t delta; /**< Intra-page bias: p_vaddr - align_down(p_vaddr). */
};

/**
 * @brief Anonymous (zero-fill) bookkeeping.
 */
struct mr_anon {
	uint32_t tag; /**< Optional accounting or debug identifier, e.g. "bss" or "heap". */
};

/**
 * @brief Device-backed bookkeeping.
 */
struct mr_device {
	paddr_t paddr; /**< Physical base address of the device region. */
};

/**
 * @brief Represents a virtual memory area (VMA).
 *
 * For an ELF segment, the FILE region covers
 * `[vstart, vstart + align_up(delta + p_filesz))`. Reads are clamped to
 * `[file_lo, file_hi)`; any unread tail is zeroed. If `p_memsz >
 * p_filesz`, a second ANON region covers the remainder:
 * `[align_up(p_vaddr + p_filesz), align_up(p_vaddr + p_memsz))`.
 *
 * @note Fault-time algorithm for a FILE region: let `VA =
 * align_down(fault_addr)`; `page_off = VA - start`; `file_off =
 * file.file_lo + page_off`; `init_left = file.file_hi - file_off`;
 * `to_read = init_left > 0 ? min(PAGE_SIZE, init_left) : 0`. Read
 * `to_read` bytes, then zero the rest of the page.
 *
 * @note Fault-time algorithm for an ANON region: allocate a zeroed
 * page and map it with `prot`.
 */
struct memory_region {
	uptr start;		      /**< VMA start, inclusive. Page-aligned. */
	uptr end;		      /**< VMA end, exclusive. Page-aligned. */

	unsigned long prot;	      /**< PROT_READ/WRITE/EXEC (ELF p_flags -> prot). */
	unsigned long flags;	      /**< MAP_PRIVATE/SHARED and other VM bits. */

	enum mr_kind kind;	      /**< Which member of the union below is valid. */
	bool is_private;	      /**< True for MAP_PRIVATE. Triggers copy-on-write on the first write. */

	union {
		struct mr_file file;  /**< Valid when kind == MR_FILE. */
		struct mr_anon anon;  /**< Valid when kind == MR_ANON. */
		struct mr_device dev; /**< Valid when kind == MR_DEVICE. */
	};

	struct address_space* owner;  /**< Owning address space. */
	struct list_head list;	      /**< Link in address_space::mr_list. */
};

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

int check_access(struct address_space* vas, vaddr_t vaddr, bool need_read, bool need_write, bool need_exec);

struct memory_region* get_region(struct address_space* vas, vaddr_t vaddr);

void add_region(struct address_space* vas, struct memory_region* mr);

void remove_region(struct memory_region* mr);

void address_space_init();

struct memory_region* alloc_mem_region(uptr start, uptr end, unsigned long prot, unsigned long flags);

void destroy_mem_region(struct memory_region* mr);
int address_space_dup(struct address_space* dest, struct address_space* src);

void unmap_region(struct address_space* vas, struct memory_region* mr);
void address_space_destroy(struct address_space* vas);

void vas_set_pml4(struct address_space* vas, pgd_t* pml4);

int map_region(struct address_space* vas,
	       struct mr_file file,
	       uptr start,
	       uptr end,
	       unsigned long prot,
	       unsigned long flags);

int map_device_region(struct address_space* vas,
		      uptr start,
		      uptr end,
		      paddr_t paddr,
		      unsigned long prot,
		      unsigned long flags);

struct address_space* alloc_address_space();

/** @} */
