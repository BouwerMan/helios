/**
 * @file kernel/syscall.c
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

#include <arch/idt.h>
#include <drivers/console.h>
#include <helios/syscall.h>
#include <kernel/dmesg.h>
#include <kernel/errno.h>
#include <kernel/irq_log.h>
#include <kernel/syscall.h>
#include <kernel/tasks/scheduler.h>
#include <mm/mmap.h>
#include <stddef.h>
#include <stdio.h>
#include <util/log.h>

/*******************************************************************************
* Private Function Prototypes
*******************************************************************************/

[[gnu::always_inline]]
static inline void SYSRET(struct registers* r, u64 val)
{
	r->rax = val; // Set the return value
}

void sys_write(struct registers* r)
{
	// rdi: file descriptor, rsi: buffer, rdx: size
	if (r->rdi != 1) { // Only handle stdout for now
		return;
	}

	const char* buf = (const char*)r->rsi;
	size_t size = r->rdx;

	// TODO: Actually use task resources :)
	extern struct vfs_file* g_kernel_console;
	vfs_file_write(g_kernel_console, buf, size);

	SYSRET(r, size); // Return the number of bytes written
}

void sys_test_cow(struct registers* r)
{
	log_info("Setting up CoW test scenario for current task...");

	struct task* task = get_current_task();
	struct address_space* vas = task->vas;

	// Define two distinct virtual addresses for the test
	vaddr_t vaddr1 = 0x100000;
	vaddr_t vaddr2 = 0x200000;

	// 1. Allocate a single physical page
	struct page* page = alloc_page(AF_NORMAL);
	if (!page) {
		// log_error("CoW test setup failed: OOM");
		r->rax = -1; // Return an error
		return;
	}
	paddr_t paddr = page_to_phys(page);
	log_info("Allocated page paddr: %lx", paddr);

	// 2. The page starts with ref_count = 1. We need it to be 2.
	get_page(page); // Manually increment ref_count to 2

	// 3. Create two read-only mappings to the same physical page
	flags_t flags = PAGE_PRESENT | PAGE_USER; // Read-only by default
	vmm_map_page(vas->pml4, vaddr1, paddr, flags);
	vmm_map_page(vas->pml4, vaddr2, paddr, flags);

	// For demonstration, let's write some initial data from the kernel
	// so the user process can see it.
	char* kernel_ptr = (char*)PHYS_TO_HHDM(paddr);
	kernel_ptr[0] = 'A';
	kernel_ptr[1] = '\0';

	log_info(
		"CoW test setup complete. Page %p mapped at 0x%lx and 0x%lx, ref_count=%d",
		(void*)paddr,
		vaddr1,
		vaddr2,
		atomic_read(&page->ref_count));

	irq_log_flush();
	console_flush();

	log_debug("rflags: %lx, saved_rflags: %lx", r->rflags, r->saved_rflags);

	r->rax = 0; // Return success
}

void sys_mmap(struct registers* r)
{
	void* addr = (void*)r->rdi;
	size_t length = (size_t)r->rsi;
	int prot = (int)r->rdx;
	int flags = (int)r->r10;
	int fd = (int)r->r8;
	off_t offset = (off_t)r->r9;

	void* map = mmap_sys(addr, length, prot, flags, fd, offset);
	SYSRET(r, (uptr)map);
}

void sys_exit(struct registers* r)
{
	task_end((int)r->rdi);
}

void sys_waitpid(struct registers* r)
{
	pid_t pid = (pid_t)r->rdi;
	int* status = (int*)r->rsi;
	int options = (int)r->rdx;
	(void)options;

	struct task* task = get_current_task();

	if (list_empty(&task->children)) {
		SYSRET(r, -ECHILD);
		return;
	}

retry:
	disable_preemption();

	// Iterate through children, find terminated, then reap and return
	struct task* child = nullptr;
	list_for_each_entry (child, &task->children, sibling) {
		bool pid_match = pid == -1 || child->pid == pid;
		bool child_terminated = child->state == TERMINATED;
		// Child isn't zombie
		if (!pid_match || !child_terminated) {
			continue;
		}

		if (status) {
			*status = child->exit_code;
		}
		pid_t child_pid = child->pid;

		reap_task(child);

		enable_preemption();
		SYSRET(r, child_pid);
		return;
	}

	// No zombies found yet, so we block and wait for a child to exit.
	enable_preemption();
	waitqueue_sleep(&child->parent_wq);
	goto retry;
}

typedef void (*handler)(struct registers* r);
static const handler syscall_handlers[] = {
	[SYS_WRITE] = sys_write,
	[SYS_MMAP] = sys_mmap,
	[SYS_EXIT] = sys_exit,
	[SYS_WAITPID] = sys_waitpid,
};

static constexpr int SYSCALL_COUNT =
	sizeof(syscall_handlers) / sizeof(syscall_handlers[0]);

/*
 * Linux-style syscalls use specific registers to pass arguments:
 * - rax: System call number
 * - rdi: First argument
 * - rsi: Second argument
 * - rdx: Third argument
 * - r10: Fourth argument
 * - r8:  Fifth argument
 * - r9:  Sixth argument
 */
void syscall_handler(struct registers* r)
{
	if (r->rax > SYSCALL_COUNT) return;
	handler func = syscall_handlers[r->rax];
	if (func) func(r);
}

void syscall_init()
{
	isr_install_handler(SYSCALL_INT, syscall_handler);
}
