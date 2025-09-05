/**
 * @file kernel/tasks/fork.c
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
#include <arch/mmu/vmm.h>
#include <kernel/tasks/fork.h>
#include <kernel/tasks/scheduler.h>
#include <lib/log.h>
#include <lib/string.h>
#include <mm/address_space.h>

/**
 * do_fork: Syscall handler for fork().
 *
 * Allocates a new task and copies parent's state
 */
pid_t do_fork(struct registers* regs)
{

	struct task* parent = get_current_task();

	struct task* child = __alloc_task();
	if (!child) {
		enable_preemption();
		return -1;
	}

	int res = copy_thread_state(child, regs);
	if (res < 0) {
		enable_preemption();
		log_error("Could not copy thread state to child");
		return res;
	}
	child->regs->rax = 0;

	child->type = USER_TASK;
	child->parent = parent;

	strncpy(child->name, parent->name, MAX_TASK_NAME_LEN);
	child->name[MAX_TASK_NAME_LEN - 1] = '\0';

	// GDB BREAKPOINT
	vas_set_pml4(child->vas, (pgd_t*)vmm_create_address_space());
	res = address_space_dup(child->vas, parent->vas);
	if (res < 0) {
		// GDB BREAKPOINT
		enable_preemption();
		log_error("Could not duplicate address space");
		return -1;
	}

	for (int i = 0; i < MAX_RESOURCES; i++) {
		if (parent->resources[i]) {
			child->resources[i] = parent->resources[i];
			child->resources[i]->ref_count++;
		}
	}

	disable_preemption();
	child->state = READY;
	__task_add(child);

	enable_preemption();
	return child->pid;
}
