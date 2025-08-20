/**
 * @file kernel/exec.c
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

#include <arch/gdt/gdt.h>
#include <arch/mmu/vmm.h>
#include <arch/regs.h>
#include <kernel/exec.h>
#include <kernel/panic.h>
#include <mm/mmap.h>
#include <mm/page.h>
#include <mm/page_alloc.h>
#include <mm/page_alloc_flags.h>
#include <string.h>

/*******************************************************************************
* Private Function Prototypes
*******************************************************************************/

/**
 * @brief Validates the ELF file header.
 *
 * @param header Pointer to the ELF file header.
 */
static bool validate(struct elf_file_header* header);
/**
 * @brief Loads a program header from an ELF file into the task's address space.
 *
 * @param task Pointer to the task structure.
 * @param elf Pointer to the ELF file.
 * @param prog Pointer to the ELF program header.
 * @return 0 on success, -1 on failure.
 */
static int load_program_header(struct task* task,
			       void* elf,
			       struct elf_program_header* prog);

/**
 * @brief Sets up the user stack for the task.
 *
 * @param task Pointer to the task structure.
 * @param stack_base The base address of the stack.
 * @param stack_pages The number of pages to allocate for the stack.
 * @return 0 on success, -1 on failure.
 */
static int setup_user_stack(struct task* task,
			    uptr stack_base,
			    size_t stack_pages);

/*******************************************************************************
* Public Function Definitions
*******************************************************************************/

int load_elf(struct task* task, struct elf_file_header* header)
{
	kassert(header != NULL && "execve: header is NULL");

	if (!validate(header)) {
		panic("execve: Invalid ELF header");
	}

	log_info("Loading ELF binary with entry point at 0x%lx", header->entry);

	if (header->type != ET_EXE) {
		log_error("Invalid elf type: %d", header->type);
		return -1;
	}

	log_debug("Valid type, reading program headers");

	uptr highest = 0;
	struct elf_program_header* prog =
		(struct elf_program_header*)((uintptr_t)header +
					     header->header_size);
	for (size_t i = 0; i < header->program_header_entry_count; i++) {

		log_debug(
			"ELF Program Header: type=0x%x, flags=0x%x, offset=0x%lx,"
			" virtual_address=0x%lx, size_in_file=0x%lx, size_in_memory=0x%lx, align=0x%lx",
			prog->type,
			prog->flags,
			prog->offset,
			prog->virtual_address,
			prog->size_in_file,
			prog->size_in_memory,
			prog->align);

		switch (prog->type) {
		case PT_LOAD:
			if (load_program_header(task, header, prog) < 0) {
				// TODO: Free previous program sections
				log_error("Failed to load program header");
				return -1;
			}
			uptr end = prog->virtual_address + prog->size_in_memory;
			if (end > highest) highest = end;
			break;
		default:
			log_error("Unknown program type %d", prog->type);
			return -1;
		}
		prog++;
	}

	uptr stack_base = align_up_page(highest);
	int err = setup_user_stack(task, stack_base, STACK_SIZE_PAGES);

	if (err < 0) {
		log_error("Failed to setup user stack");
		return -1;
	}

	task->regs->rip = header->entry; // Set the entry point

	task->regs->cs = USER_CS;
	task->regs->ds = USER_DS;
	task->regs->ss = USER_DS;
	return 0;
}

/*******************************************************************************
* Private Function Definitions
*******************************************************************************/

static bool validate(struct elf_file_header* header)
{
	kassert(header != NULL && "elf_validate: header is NULL");

	log_debug("Validating ELF header at %p", (void*)header);

	if (header->id[EI_MAG0] != ELFMAG0) {
		log_error("ELF Header EI_MAG0 incorrect.\n");
		return false;
	}
	if (header->id[EI_MAG1] != ELFMAG1) {
		log_error("ELF Header EI_MAG1 incorrect.\n");
		return false;
	}
	if (header->id[EI_MAG2] != ELFMAG2) {
		log_error("ELF Header EI_MAG2 incorrect.\n");
		return false;
	}
	if (header->id[EI_MAG3] != ELFMAG3) {
		log_error("ELF Header EI_MAG3 incorrect.\n");
		return false;
	}

	// TODO: Verify more like endian and such

	return true;
}

static int load_program_header(struct task* task,
			       void* elf,
			       struct elf_program_header* prog)
{
	u64* pml4 = (u64*)PHYS_TO_HHDM(task->vas->pml4_phys);

	size_t pages = CEIL_DIV(prog->size_in_memory, PAGE_SIZE);
	void* free_pages = get_free_pages(AF_KERNEL, pages);
	if (!free_pages) {
		log_error("Failed to allocate memory");
		return -1;
	}

	uptr paddr_start = HHDM_TO_PHYS(free_pages);
	uptr vaddr_start = (uptr)prog->virtual_address;

	flags_t page_flags = PAGE_PRESENT | PAGE_USER;
	page_flags |= prog->flags & PF_WRITE ? PAGE_WRITE : 0;
	page_flags |= prog->flags & PF_EXEC ? 0 : PAGE_NO_EXECUTE;

	unsigned long prot = (prog->flags & PF_EXEC) ? PROT_EXEC : 0;
	prot |= prog->flags & PF_WRITE ? PROT_WRITE : 0;
	prot |= prog->flags & PF_READ ? PROT_READ : 0;

	map_region(task->vas,
		   vaddr_start,
		   vaddr_start + (pages * PAGE_SIZE),
		   prot,
		   prog->flags);

	for (size_t i = 0; i < pages; i++) {
		uptr vaddr = vaddr_start + i * PAGE_SIZE;
		uptr paddr = paddr_start + i * PAGE_SIZE;
		int err = vmm_map_page((pgd_t*)pml4, vaddr, paddr, page_flags);

		if (err) {
			log_error("Failed to map page");
			return -1;
		}
		log_debug("Mapped vaddr: %lx, to paddr: %lx", vaddr, paddr);
	}

	// I am copying into kvaddr which is the same memory as vaddr in the other address space.
	void* kvaddr = (void*)free_pages;

	void* data = (void*)((uptr)elf + prog->offset);
	log_debug("Copying data at %p to vaddr %p, size: %zu",
		  data,
		  kvaddr,
		  prog->size_in_file);
	memcpy(kvaddr, data, prog->size_in_file);

	return 0;
}

static int setup_user_stack(struct task* task,
			    uptr stack_base,
			    size_t stack_pages)
{
	constexpr flags_t page_flags = PAGE_PRESENT | PAGE_WRITE | PAGE_USER |
				       PAGE_NO_EXECUTE;
	u64* pml4 = (u64*)PHYS_TO_HHDM(task->vas->pml4_phys);
	uptr stack_top = stack_base + stack_pages * PAGE_SIZE;
	log_debug("Setting up user stack at base: 0x%lx, top: 0x%lx",
		  stack_base,
		  stack_top);

	uptr stack = HHDM_TO_PHYS(get_free_pages(AF_KERNEL, stack_pages));

	map_region(task->vas, stack_base, stack_top, PROT_READ | PROT_WRITE, 0);

	for (size_t i = 0; i < stack_pages; i++) {
		uptr vaddr = stack_base + i * PAGE_SIZE;
		uptr paddr = stack + i * PAGE_SIZE;
		int err = vmm_map_page((pgd_t*)pml4, vaddr, paddr, page_flags);

		if (err) {
			log_error("Failed to map stack page at: 0x%lx", paddr);
			return -1;
		}
		log_debug("Mapped vaddr: %lx, to paddr: %lx", vaddr, paddr);
	}

	task->regs->rsp =
		stack_top; // Set the stack pointer to the top of the stack
	return 0;
}
