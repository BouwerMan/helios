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

#include "fs/vfs.h"
#include <arch/gdt/gdt.h>
#include <arch/mmu/vmm.h>
#include <arch/regs.h>
#include <kernel/exec.h>
#include <kernel/panic.h>
#include <lib/string.h>
#include <mm/mmap.h>
#include <mm/page.h>
#include <mm/page_alloc.h>
#include <mm/page_alloc_flags.h>
#include <uapi/helios/errno.h>

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
 * FIXME:
 * @param task Pointer to the task structure.
 * @param elf Pointer to the ELF file.
 * @param prog Pointer to the ELF program header.
 * @return 0 on success, -1 on failure.
 */
static int load_program_header(struct task* task,
			       struct vfs_inode* inode,
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

int exec(struct task* task, const char* path)
{
	int fd = vfs_open(path, O_RDONLY);
	if (fd < 0) {
		log_error("execve: Failed to open file %s: %d", path, fd);
		return -ENOENT;
	}

	__load_elf(task, get_file(fd));

	vfs_close(fd);

	return 0;
}

int __load_elf(struct task* task, struct vfs_file* file)
{
	void* temp_buf = get_free_page(AF_KERNEL);
	if (!temp_buf) {
		log_error("Failed to allocate temporary buffer");
		return -ENOMEM;
	}

	vfs_file_read(file, temp_buf, PAGE_SIZE);

	struct elf_file_header* header = temp_buf;

	if (!validate(header)) {
		log_error("Invalid ELF header");
		return -ENOEXEC;
	}

	log_info("Loading ELF binary with entry point at 0x%lx", header->entry);

	if (header->type != ET_EXE) {
		log_error("Invalid elf type: %d", header->type);
		return -1;
	}

	log_debug("Valid type, reading program headers");

	// TODO: Proper section header handling for .bss and such

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
			if (load_program_header(
				    task, file->dentry->inode, header, prog) <
			    0) {
				// TODO: Free previous program sections
				log_error("Failed to load program header");
				return -1;
			}
			uptr end = align_up_page(prog->virtual_address +
						 prog->size_in_memory);
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

	free_page(temp_buf);
	return 0;
}

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

	// TODO: Proper section header handling for .bss and such

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
			if (load_program_header(task, nullptr, header, prog) <
			    0) {
				// TODO: Free previous program sections
				log_error("Failed to load program header");
				return -1;
			}
			uptr end = align_up_page(prog->virtual_address +
						 prog->size_in_memory);
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
			       struct vfs_inode* inode,
			       void* elf,
			       struct elf_program_header* prog)
{
	size_t pages = CEIL_DIV(prog->size_in_memory, PAGE_SIZE);

	vaddr_t vaddr_start = align_down_page((vaddr_t)prog->virtual_address);
	vaddr_t vaddr_end = vaddr_start + (pages * PAGE_SIZE);

	unsigned long prot = (prog->flags & PF_EXEC) ? PROT_EXEC : 0;
	prot |= prog->flags & PF_WRITE ? PROT_WRITE : 0;
	prot |= prog->flags & PF_READ ? PROT_READ : 0;

	if (inode) {
		// Map as file backed
		map_region(task->vas,
			   inode,
			   (off_t)prog->offset,
			   vaddr_start,
			   vaddr_end,
			   prot,
			   MAP_PRIVATE);
	} else {
		// For now we will keep supporting non anonymous mapping
		map_region(task->vas,
			   nullptr,
			   0,
			   vaddr_start,
			   vaddr_end,
			   prot,
			   MAP_PRIVATE | MAP_ANONYMOUS);

		void* data = (void*)((uptr)elf + prog->offset);

		vmm_write_region(
			task->vas, vaddr_start, data, prog->size_in_file);
	}

	return 0;
}

static int setup_user_stack(struct task* task,
			    uptr stack_base,
			    size_t stack_pages)
{
	uptr stack_top = stack_base + stack_pages * PAGE_SIZE;
	log_debug("Setting up user stack at base: 0x%lx, top: 0x%lx",
		  stack_base,
		  stack_top);

	map_region(task->vas,
		   nullptr,
		   -1,
		   stack_base,
		   stack_top,
		   PROT_READ | PROT_WRITE,
		   MAP_ANONYMOUS | MAP_PRIVATE | MAP_GROWSDOWN);

	task->regs->rsp =
		stack_top; // Set the stack pointer to the top of the stack
	return 0;
}
