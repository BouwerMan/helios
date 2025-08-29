#include <arch/mmu/vmm.h>
#include <drivers/fs/vfs.h>
#include <kernel/tasks/fork.h>
#include <kernel/tasks/scheduler.h>
#include <mm/address_space.h>
#include <string.h>
#include <util/log.h>

/**
 * do_fork: Syscall handler for fork().
 *
 * Allocates a new task and copies parent's state
 */
pid_t do_fork(struct registers* regs)
{
	disable_preemption();

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

	vas_set_pml4(child->vas, (pgd_t*)vmm_create_address_space());
	res = address_space_dup(child->vas, parent->vas);
	if (res < 0) {
		log_error("Could not duplicate address space");
		return -1;
	}

	for (int i = 0; i < MAX_RESOURCES; i++) {
		if (parent->resources[i]) {
			child->resources[i] = parent->resources[i];
			child->resources[i]->ref_count++;
		}
	}

	child->state = READY;
	__task_add(child);

	enable_preemption();
	return child->pid;
}
