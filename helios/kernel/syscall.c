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

#include <uapi/asm/syscall.h>
#include <uapi/helios/errno.h>

#include "arch/idt.h"
#include "drivers/console.h"
#include "kernel/exec.h"
#include "kernel/syscall.h"
#include "kernel/tasks/fork.h"
#include "kernel/tasks/scheduler.h"
#include "lib/log.h"
#include "mm/kmalloc.h"
#include "mm/mmap.h"

[[gnu::always_inline]]
static inline void SYSRET(struct registers* r, u64 val)
{
	r->rax = val; // Set the return value
}

long sys_read(struct registers* r)
{
	int fd = (int)r->rdi;
	void* buf = (void*)r->rsi;
	size_t size = (size_t)r->rdx;

	return vfs_read(fd, buf, size);
}

long sys_write(struct registers* r)
{
	int fd = (int)r->rdi;
	const char* buf = (const char*)r->rsi;
	size_t size = (size_t)r->rdx;

	return vfs_write(fd, buf, size);
}

long sys_mmap(struct registers* r)
{
	void* addr = (void*)r->rdi;
	size_t length = (size_t)r->rsi;
	int prot = (int)r->rdx;
	int flags = (int)r->r10;
	int fd = (int)r->r8;
	off_t offset = (off_t)r->r9;

	void* map = mmap_sys(addr, length, prot, flags, fd, offset);
	return (long)map;
}

long sys_exit(struct registers* r)
{
	task_end((int)r->rdi);
	return 0; // Never reached
}

long sys_fork(struct registers* r)
{
	pid_t pid = do_fork(r);

	return pid;
}

long sys_waitpid(struct registers* r)
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
		return -ECHILD;
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

		return child_pid;
	}

wait:
	// No zombies found yet, so we block and wait for a child to exit.
	waitqueue_sleep(&task->parent_wq);
	goto retry;
}

long sys_getpid(struct registers* r)
{
	(void)r;

	struct task* task = get_current_task();

	return task->pid;
}

long sys_getppid(struct registers* r)
{
	(void)r;

	struct task* task = get_current_task();
	if (task->parent) {
		return task->parent->pid;
	} else {
		return 0; // No parent
	}
}

long sys_exec(struct registers* r)
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
	return ret;
}

long sys_getcwd(struct registers* r)
{
	void* buf = (void*)r->rdi;
	size_t size = (size_t)r->rsi;

	struct task* task = get_current_task();
	char* path = dentry_to_abspath(task->cwd);
	size_t cwd_len = strlen(path);
	if (cwd_len + 1 > size) {
		kfree(path);
		return (long)NULL;
	}

	for (size_t i = 0; i < size; i++) {
		((char*)buf)[i] = path[i];
	}
	((char*)buf)[cwd_len] = '\0';

	kfree(path);
	return (long)buf;
}

long sys_chdir(struct registers* r)
{
	const char* path = (const char*)r->rdi;

	struct task* task = get_current_task();
	struct vfs_dentry* dentry = vfs_lookup(path);
	if (dentry && dentry->inode &&
	    dentry->inode->filetype == FILETYPE_DIR) {
		dput(task->cwd);
		task->cwd = dentry;
		return 0;
	}

	return -ENOENT;
}

long sys_getdents(struct registers* r)
{
	int fd = (int)r->rdi;
	struct dirent* dirp = (struct dirent*)r->rsi;
	size_t count = (size_t)r->rdx;

	return vfs_getdents(fd, dirp, count);
}

long sys_open(struct registers* r)
{
	const char* path = (const char*)r->rdi;
	int flags = (int)r->rsi;

	return vfs_open(path, flags);
}

long sys_close(struct registers* r)
{
	int fd = (int)r->rdi;

	return vfs_close(fd);
}

long sys_access(struct registers* r)
{
	const char* path = (const char*)r->rdi;
	int amode = (int)r->rsi;

	return vfs_access(path, amode);
}

long sys_shutdown(struct registers* r)
{
	(void)r;
	set_log_mode(LOG_DIRECT);
	console_flush();

	// QEMU shutdown command
	outword(0x604, 0x2000);
	outb(0xF4, 0);

	return 0;
}

typedef long (*sys_handler_t)(struct registers* r);
static const sys_handler_t syscall_handlers[] = {
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
_Static_assert(SYSCALL_COUNT == SYS_SYSCALL_COUNT, "SYSCALL_COUNT mismatch");

void syscall_handler(struct registers* r)
{
	if (r->rax > SYSCALL_COUNT) return;
	sys_handler_t func = syscall_handlers[r->rax];
	if (func) {
		struct task* task = get_current_task();
		task->regs = r;
		// TODO: Return int
		long ret = func(r);
		SYSRET(r, (u64)ret);
	}
}

void syscall_init()
{
	isr_install_handler(SYSCALL_INT, syscall_handler);
}
