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

#include "kernel/timer.h"
#include "mm/kmalloc.h"
#include <arch/idt.h>
#include <arch/mmu/vmm.h>
#include <drivers/console.h>
#include <kernel/exec.h>
#include <kernel/irq_log.h>
#include <kernel/panic.h>
#include <kernel/syscall.h>
#include <kernel/tasks/fork.h>
#include <kernel/tasks/scheduler.h>
#include <lib/log.h>
#include <lib/string.h>
#include <mm/mmap.h>
#include <mm/page.h>
#include <uapi/asm/syscall.h>
#include <uapi/helios/errno.h>

/*******************************************************************************
* Private Function Prototypes
*******************************************************************************/

[[gnu::always_inline]]
static inline void SYSRET(struct registers* r, u64 val)
{
	r->rax = val; // Set the return value
}

void sys_read(struct registers* r)
{
	// rdi: file descriptor, rsi: buffer, rdx: size
	int fd = (int)r->rdi;
	void* buf = (void*)r->rsi;
	size_t size = r->rdx;

	if (fd != 0) { // Only handle stdin for now
		return;
	}

	ssize_t read = vfs_read(fd, buf, size);
	SYSRET(r, (u64)read); // Return the number of bytes read
}

void sys_write(struct registers* r)
{
	// rdi: file descriptor, rsi: buffer, rdx: size
	int fd = (int)r->rdi;
	const char* buf = (const char*)r->rsi;
	size_t size = r->rdx;

	// TODO: Actually use task resources :)
	// extern struct vfs_file* g_kernel_console;
	// vfs_file_write(g_kernel_console, buf, size);
	vfs_write(fd, buf, size);

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
		r->rax = (u64)-1; // Return an error
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

void sys_fork(struct registers* r)
{
	pid_t pid = do_fork(r);
	SYSRET(r, (u64)pid);
}

void sys_waitpid(struct registers* r)
{
	pid_t pid = (pid_t)r->rdi;
	int* status = (int*)r->rsi;
	int options = (int)r->rdx;
	(void)options;

	struct task* task = get_current_task();

	if (list_empty(&task->children)) {
		if (task->pid == INIT_PID) {
			goto wait;
		}
		SYSRET(r, (u64)-ECHILD);
		return;
	}

retry:
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

		SYSRET(r, (u64)child_pid);
		return;
	}

wait:
	// No zombies found yet, so we block and wait for a child to exit.
	waitqueue_sleep(&task->parent_wq);
	goto retry;
}

void sys_getpid(struct registers* r)
{
	struct task* task = get_current_task();
	SYSRET(r, (u64)task->pid);
}

void sys_getppid(struct registers* r)
{
	struct task* task = get_current_task();
	if (task->parent) {
		SYSRET(r, (u64)task->parent->pid);
	} else {
		SYSRET(r, 0); // No parent
	}
}

#include <kernel/limine_requests.h>

/**
 * @rdi: file name (const char*)
 * @rsi: argv (char**)
 * @rdx: envp (char**)
 */
void sys_exec(struct registers* r)
{
	const char* name = (const char*)r->rdi;
	const char** argv = (const char**)r->rsi;
	const char** envp = (const char**)r->rdx;

	// TODO: Don't trust user pointers

	struct exec_context* ctx = prepare_exec(name, argv, envp);
	if (!ctx) {
		SYSRET(r, (u64)-ENOENT);
	}

	struct task* current = get_current_task();
	int ret = commit_exec(current, ctx);

	// When we return from syscall, we'll return to the new process
	SYSRET(r, (u64)ret);
}

// rdi=buf, rsi=size
void sys_getcwd(struct registers* r)
{
	void* buf = (void*)r->rdi;
	size_t size = (size_t)r->rsi;

	struct task* task = get_current_task();
	char* path = dentry_to_abspath(task->cwd);
	size_t cwd_len = strlen(path);
	if (cwd_len + 1 > size) {
		kfree(path);
		SYSRET(r, (u64)NULL);
		return;
	}

	for (size_t i = 0; i < size; i++) {
		((char*)buf)[i] = path[i];
	}
	((char*)buf)[cwd_len] = '\0';

	kfree(path);
	SYSRET(r, (u64)buf);
}

// rdi=path
void sys_chdir(struct registers* r)
{
	struct task* task = get_current_task();
	struct vfs_dentry* dentry = vfs_lookup((const char*)r->rdi);
	if (dentry && dentry->inode &&
	    dentry->inode->filetype == FILETYPE_DIR) {
		dput(task->cwd);
		task->cwd = dentry;
		SYSRET(r, 0);
		return;
	}
	SYSRET(r, (u64)-ENOENT);
}

void sys_getdents(struct registers* r)
{
	int fd = (int)r->rdi;
	struct dirent* dirp = (struct dirent*)r->rsi;
	size_t count = (size_t)r->rdx;

	ssize_t res = vfs_getdents(fd, dirp, count);
	SYSRET(r, (u64)res);
}

void sys_open(struct registers* r)
{
	const char* path = (const char*)r->rdi;
	int flags = (int)r->rsi;

	int fd = vfs_open(path, flags);
	SYSRET(r, (u64)fd);
}

void sys_close(struct registers* r)
{
	int fd = (int)r->rdi;

	int res = vfs_close(fd);
	SYSRET(r, (u64)res);
}

void sys_access(struct registers* r)
{
	const char* path = (const char*)r->rdi;
	int amode = (int)r->rsi;

	int res = vfs_access(path, amode);
	SYSRET(r, (u64)res);
}

void sys_shutdown(struct registers* r)
{
	(void)r;
	set_log_mode(LOG_DIRECT);
	irq_log_flush();
	console_flush();

	// log_warn("Shutting down in 1 second");
	// sleep(1000);

	// QEMU shutdown command
	outword(0x604, 0x2000);
	outb(0xF4, 0);
}

typedef void (*handler)(struct registers* r);
static const handler syscall_handlers[] = {
	[SYS_READ] = sys_read,	     [SYS_WRITE] = sys_write,
	[SYS_MMAP] = sys_mmap,	     [SYS_EXIT] = sys_exit,
	[SYS_WAITPID] = sys_waitpid, [SYS_FORK] = sys_fork,
	[SYS_GETPID] = sys_getpid,   [SYS_GETPPID] = sys_getppid,
	[SYS_EXEC] = sys_exec,	     [SYS_GETCWD] = sys_getcwd,
	[SYS_CHDIR] = sys_chdir,     [SYS_GETDENTS] = sys_getdents,
	[SYS_OPEN] = sys_open,	     [SYS_CLOSE] = sys_close,
	[SYS_ACCESS] = sys_access,   [SYS_SHUTDOWN] = sys_shutdown,
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
	if (func) {
		struct task* task = get_current_task();
		task->regs = r;
		// TODO: Return int
		func(r);
	}
}

void syscall_init()
{
	isr_install_handler(SYSCALL_INT, syscall_handler);
}
