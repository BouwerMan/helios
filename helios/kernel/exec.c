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
#include "kernel/tasks/scheduler.h"
#include "mm/address_space.h"
#include "mm/kmalloc.h"
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
 * @param ctx The exec context containing the new address space.
 * @param inode Pointer to the inode of the ELF file, or nullptr for anonymous mapping.
 * @param elf Pointer to the ELF file. If anonymous mapping is used, this is required to copy data from.
 * @param prog Pointer to the ELF program header.
 * @return 0 on success, -1 on failure.
 */
static int load_program_header(struct exec_context* ctx,
			       struct vfs_inode* inode,
			       void* elf,
			       struct elf_program_header* prog);

/**
 * @brief Sets up the user stack for the task.
 *
 * @param ctx The exec context containing the new address space.
 * @param stack_base The top address of the stack.
 * @param stack_pages The number of pages to allocate for the stack.
 * @return 0 on success, -1 on failure.
 */
static int setup_user_stack(struct exec_context* ctx,
			    uptr stack_top,
			    size_t stack_pages,
			    const char** argv,
			    const char** envp);

/*******************************************************************************
* Public Function Definitions
*******************************************************************************/

struct exec_context* prepare_exec(const char* path,
				  const char** argv,
				  const char** envp)
{
	struct exec_context* ctx =
		(struct exec_context*)kzalloc(sizeof(struct exec_context));
	if (!ctx) {
		log_error("OOM when creating ctx");
		return nullptr;
	}

	ctx->new_vas = alloc_address_space();
	if (!ctx->new_vas) {
		kfree(ctx);
		return nullptr;
	}
	u64* new_pml4 = vmm_create_address_space();
	vas_set_pml4(ctx->new_vas, (pgd_t*)new_pml4);

	int fd = vfs_open(path, O_RDONLY);
	if (fd < 0) {
		log_error("Failed to open file %s: %d", path, fd);
		return nullptr;
	}

	__load_elf(ctx, get_file(fd));

	vfs_close(fd);

	int err = setup_user_stack(
		ctx, DEFAULT_STACK_TOP, STACK_SIZE_PAGES, argv, envp);

	if (err < 0) {
		log_error("Failed to setup user stack");
		return nullptr;
	}

	ctx->prepared = true;
	return ctx;
}

int commit_exec(struct task* task, struct exec_context* ctx)
{

	if (!ctx || !ctx->prepared) {
		return -EINVAL;
	}
	disable_preemption();

	struct task* current = get_current_task();
	pgd_t* old_pml4 = task->vas->pml4;
	struct address_space* old_vas = task->vas;

	if (task == current) {
		// Switch to new address space first
		vmm_load_cr3(HHDM_TO_PHYS(ctx->new_vas->pml4));
	}

	// Now safe to update task structure
	task->vas = ctx->new_vas;

	address_space_destroy(old_vas);
	// TODO: Make sure there are no page table leaks here
	free_page(old_pml4);
	kfree(old_vas);

	memset(task->regs, 0, sizeof(struct registers));

	task->regs->rip = (u64)ctx->entry_point;
	task->regs->rsp = (u64)ctx->user_stack_top;
	task->regs->cs = USER_CS;
	task->regs->ds = USER_DS;
	task->regs->ss = USER_DS;
	task->regs->rflags = DEFAULT_RFLAGS;

	strncpy(task->name, ctx->name, MAX_TASK_NAME_LEN - 1);

	kfree(ctx);

	enable_preemption();
	return 0;
}

int __load_elf(struct exec_context* ctx, struct vfs_file* file)
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
				    ctx, file->dentry->inode, header, prog) <
			    0) {
				// TODO: Free previous program sections
				log_error("Failed to load program header");
				return -1;
			}
			break;
		default:
			log_error("Unknown program type %d", prog->type);
			return -1;
		}
		prog++;
	}

	ctx->entry_point = (void*)header->entry;
	strncpy(ctx->name, file->dentry->name, MAX_TASK_NAME_LEN - 1);
	ctx->name[MAX_TASK_NAME_LEN - 1] = '\0';

	free_page(temp_buf);
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

static int load_program_header(struct exec_context* ctx,
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
		map_region(ctx->new_vas,
			   inode,
			   (off_t)prog->offset,
			   vaddr_start,
			   vaddr_end,
			   prot,
			   MAP_PRIVATE);
	} else {
		// For now we will keep supporting non anonymous mapping
		map_region(ctx->new_vas,
			   nullptr,
			   0,
			   vaddr_start,
			   vaddr_end,
			   prot,
			   MAP_PRIVATE | MAP_ANONYMOUS);

		void* data = (void*)((uptr)elf + prog->offset);

		vmm_write_region(
			ctx->new_vas, vaddr_start, data, prog->size_in_file);
	}

	return 0;
}

static size_t arg_len(const char** args)
{
	size_t len = 0;
	if (!args) return 0;
	while (args[len])
		len++;
	return len;
}

static int setup_user_stack(struct exec_context* ctx,
			    uptr stack_top,
			    size_t stack_pages,
			    const char** argv,
			    const char** envp)
{
	uptr stack_base = stack_top - stack_pages * PAGE_SIZE;
	// uptr stack_top = stack_base + stack_pages * PAGE_SIZE;
	log_debug("Setting up user stack at base: 0x%lx, top: 0x%lx",
		  stack_base,
		  stack_top);

	map_region(ctx->new_vas,
		   nullptr,
		   -1,
		   stack_base,
		   stack_top,
		   PROT_READ | PROT_WRITE,
		   MAP_ANONYMOUS | MAP_PRIVATE | MAP_GROWSDOWN);

	// Just some defaults just in case
	// TODO: Split this into new function
	if (argv == nullptr) {
		argv = (const char*[]) { "helios", NULL };
	}
	if (envp == nullptr) {
		envp = (const char*[]) { "PATH=/bin", NULL };
	}

	struct address_space* vas = ctx->new_vas;
	uptr current_sp = stack_top;
	const char* string;

	size_t envc = arg_len(envp);
	size_t argc = arg_len(argv);
	uptr env_addrs[envc];
	uptr arg_addrs[argc];

	for (ssize_t i = (ssize_t)envc - 1; i >= 0; i--) {
		string = envp[i];
		size_t len = strlen(string) + 1;
		current_sp -= len;
		vmm_write_region(vas, current_sp, string, len);
		env_addrs[i] = current_sp;
	}

	for (ssize_t i = (ssize_t)argc - 1; i >= 0; i--) {
		string = argv[i];
		size_t len = strlen(string) + 1;
		current_sp -= len;
		vmm_write_region(vas, current_sp, string, len);
		arg_addrs[i] = current_sp;
	}

	current_sp &= (uptr)~0xF; // Align to 16 bytes

	uptr envp_arr[envc + 1];
	for (size_t i = 0; i < envc; i++) {
		envp_arr[i] = env_addrs[i];
	}
	envp_arr[envc] = 0;

	current_sp -= (envc + 1) * sizeof(uptr);
	vmm_write_region(vas, current_sp, envp_arr, (envc + 1) * sizeof(uptr));

	uptr argv_arr[argc + 1];
	for (size_t i = 0; i < argc; i++) {
		argv_arr[i] = arg_addrs[i];
	}
	argv_arr[argc] = 0;

	current_sp -= (argc + 1) * sizeof(uptr);
	vmm_write_region(vas, current_sp, argv_arr, (argc + 1) * sizeof(uptr));

	current_sp -= sizeof(size_t);
	vmm_write_region(vas, current_sp, &argc, sizeof(size_t));

	ctx->user_stack_top = (void*)current_sp;

	log_debug("User stack setup complete. Final SP: 0x%lx", current_sp);

	return 0;
}
