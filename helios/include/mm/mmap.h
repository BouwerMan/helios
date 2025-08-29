/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <helios/mman.h>
#include <kernel/types.h>
#include <mm/address_space.h>
#include <stddef.h>

#define DEF_ADDR ((void*)0x555555554000)

/**
 * @brief Creates a new memory mapping in the virtual address space of a process.
 *
 * This function is the primary way for a process to request memory from the kernel.
 * It can be used to map a file into memory (file-backed mapping) or to create
 * a region of zero-initialized memory (anonymous mapping).
 *
 * @param addr A suggested starting address for the mapping. If NULL, the kernel
 * will choose a suitable address. This is the recommended usage.
 * @param length The length of the mapping in bytes. The kernel will round this up
 * to a multiple of the system page size.
 * @param prot Describes the desired memory protection of the mapping (e.g., PROT_READ,
 * PROT_WRITE, PROT_EXEC). These can be bitwise OR'd together.
 * @param flags Determines the nature of the mapping. Key flags include MAP_PRIVATE
 * (for a private, copy-on-write mapping), MAP_SHARED (changes are
 * visible to other processes), and MAP_ANONYMOUS (the mapping is not
 * backed by a file).
 * @param fd   For file-backed mappings, this is the file descriptor of the file to
 * map. For anonymous mappings, this should be -1.
 * @param offset The offset in the file from where the mapping should start. This
 * value must be a multiple of the system page size.
 *
 * @return On success, returns a pointer to the mapped memory area. On error,
 * MAP_FAILED is returned, and errno is set to indicate the cause
 * of the error.
 */
void* mmap_sys(void* addr,
	       size_t length,
	       int prot,
	       int flags,
	       int fd,
	       off_t offset);

/**
 * @brief Removes a memory mapping.
 *
 * This function deallocates a region of virtual memory that was previously created
 * by a call to mmap(). Any further access to the unmapped region will result in a
 * segmentation fault.
 *
 * @param addr The starting address of the mapping to be removed. This must be an
 * address previously returned by a successful mmap() call.
 * @param length The total length of the memory region to unmap. All pages falling
 * entirely within the range [addr, addr + length) are unmapped.
 *
 * @return On success, returns 0. On failure, returns -1, and errno is set to
 * indicate the cause of the error.
 */
int munmap(void* addr, size_t length);
