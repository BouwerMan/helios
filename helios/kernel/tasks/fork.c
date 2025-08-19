#include <arch/mmu/vmm.h>
#include <kernel/tasks/fork.h>
#include <kernel/tasks/scheduler.h>
#include <string.h>
#include <util/log.h>

/**
 * do_fork: Syscall handler for fork().
 *
 * Allocates a new task and copies parent's state
 */
long do_fork(struct registers* regs)
{
	disable_preemption();
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

	struct task* parent = get_current_task();

	child->type = USER_TASK;
	child->parent = parent;

	strncpy(child->name, parent->name, MAX_TASK_NAME_LEN);
	child->name[MAX_TASK_NAME_LEN - 1] = '\0';

	child->vas->pml4_phys = HHDM_TO_PHYS((uptr)vmm_create_address_space());
	address_space_dup(child->vas, parent->vas);

	enable_preemption();
	return 0;
}
