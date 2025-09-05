/**
 * @file kernel/tasks/scheduler.c
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

#undef LOG_LEVEL
#define LOG_LEVEL 0
#define FORCE_LOG_REDEF
#include <lib/log.h>
#undef FORCE_LOG_REDEF

#include "fs/vfs.h"
#include <arch/gdt/gdt.h>
#include <arch/idt.h>
#include <arch/mmu/vmm.h>
#include <arch/regs.h>
#include <kernel/exec.h>
#include <kernel/limine_requests.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>
#include <kernel/tasks/scheduler.h>
#include <lib/list.h>
#include <lib/string.h>
#include <mm/address_space.h>
#include <mm/kmalloc.h>
#include <mm/page.h>
#include <mm/page_alloc.h>
#include <mm/slab.h>
#include <uapi/helios/errno.h>

/*******************************************************************************
* Global Variable Definitions
*******************************************************************************/

volatile bool need_reschedule = false;
// If > 0, preempt is disabled
static bool g_preempt_enabled = false;

struct scheduler_queue squeue = { 0 };
struct task* kernel_task;
struct task* idle_task;

/*******************************************************************************
* Private Function Prototypes
*******************************************************************************/

[[noreturn]]
extern void __switch_to(struct task* next);

/**
 * @brief Picks the next task to run from the scheduler queue.
 *
 * @return Pointer to the next task structure, or the idle task if no ready task is found.
 */
static struct task* pick_next();

static void task_remove(struct task* task);

/**
 * @brief Creates a kernel stack an imitates an interrupt frame for the given task.
 *
 * @param task Pointer to the task structure.
 * @param entry Pointer to entry function for kernel tasks
 * @return 0 on success, 1 on failure.
 */
static int create_kernel_stack(struct task* task, entry_func entry);

/**
 * Entry point for the idle task. This task halts the CPU in an infinite loop.
 */
static void idle_task_entry();

/**
 * setup_first_kernel_task - Initialize the primary kernel task
 */
static void setup_first_kernel_task();

/**
 * setup_idle_task - Create the idle task for CPU idle periods
 */
static void setup_idle_task();

/**
 * Checks whether preemption is enabled by checking the preemption counter.
 */
static inline bool preempt_is_enabled()
{
	return get_current_task()->preempt_count == 0;
}

/**
 * Helper to set task state and add the task to the chosen block list
 */
static inline void __task_block(struct task* task, struct list_head* block_list)
{
	task->state = BLOCKED;
	list_move_tail(&task->sched_list, block_list);
}

static inline bool should_reschedule()
{
	struct task* task = get_current_task();
	return need_reschedule && task->preempt_count == 0;
}

/*******************************************************************************
* Public Function Definitions
*******************************************************************************/

/**
 * Enables task preemption by decrementing the preemption counter.
 */
void enable_preemption(void)
{
	// preempt_count--;
	get_current_task()->preempt_count--;
}

/**
 * Disables task preemption by incrementing the preemption counter.
 * If the counter underflows, a panic is triggered.
 */
void disable_preemption(void)
{
	// if (++preempt_count < 0) panic("preempt count underflow");
	if (++get_current_task()->preempt_count < 0) {
		panic("preempt count underflow");
	}
}

/**
 * Retrieves the currently running task.
 *
 * @return Pointer to the current task structure.
 */
struct task* get_current_task()
{
	return squeue.current_task;
}

/**
 * Retrieves the scheduler queue.
 *
 * @return Pointer to the scheduler queue structure.
 */
struct scheduler_queue* get_scheduler_queue()
{
	return &squeue;
}

struct task* previous_task = nullptr;

/**
 * Checks if a reschedule is needed and performs a context switch if required.
 *
 * @param regs Pointer to the current task's register state.
 */
void schedule(struct registers* regs)
{
	if (!should_reschedule()) return;

	need_reschedule = false;

	struct task* prev = squeue.current_task;
	previous_task = prev;
	prev->regs = regs;
	if (prev->state != BLOCKED && prev->state != TERMINATED) {
		prev->state = READY;
	}

	struct task* new = pick_next();

	new->state = RUNNING;

	set_tss_rsp(new->kernel_stack);

	/* @ref Does not return */
	__switch_to(new);
	__builtin_unreachable();
}

/**
 * Initializes the scheduler by creating the idle and kernel tasks.
 */
void scheduler_init(void)
{
	squeue.kernel_pid_counter = KERNEL_PID_BASE;
	squeue.user_pid_counter = USER_PID_BASE;

	squeue.cache = kmalloc(sizeof(struct slab_cache));
	if (!squeue.cache) {
		log_error("OOM error from kmalloc");
		panic("OOM error from kmalloc");
	}

	list_init(&squeue.ready_list);
	list_init(&squeue.blocked_list);
	list_init(&squeue.terminated_list);

	int res = slab_cache_init(squeue.cache,
				  "Scheduler Tasks",
				  sizeof(struct task),
				  0,
				  NULL,
				  NULL);
	if (res < 0) {
		log_error(
			"Could not init scheduler tasks cache, slab_cache_init() returned %d",
			res);
		panic("Scheduler tasks cache init failure");
	}

	setup_first_kernel_task();
	setup_idle_task();

	squeue.inited = true;
	log_debug("Probably inited the scheduler");
	scheduler_dump();
	g_preempt_enabled = true;
}

bool is_scheduler_init()
{
	return squeue.inited;
}

/**
 * kthread_create - Creates new kernel thread
 * @name: Name of thread
 * @entry: Entry function to execute on first run
 *
 * User must call kthread_run after this function for the scheduler
 * to schedule it.
 *
 * Return: Task that was created, nullptr if error
 */
struct task* kthread_create(const char* name, entry_func entry)
{
	disable_preemption();
	struct task* task = __alloc_task();
	if (!task) {
		enable_preemption();
		log_error("Failed to allocate task structure");
		return nullptr;
	}
	if (create_kernel_stack(task, entry) < 0) {
		enable_preemption();
		log_error("Failed to create kernel stack");
		slab_free(squeue.cache, task);
		return nullptr;
	}

	task->type = KERNEL_TASK;
	task->parent = kernel_task;
	task->pid = squeue.kernel_pid_counter++;

	// Kernel threads don't get their own address space
	// Nor do they get any regions (for now)
	vas_set_pml4(task->vas, (pgd_t*)PHYS_TO_HHDM(vmm_read_cr3()));
	// task->vas->pml4_phys = vmm_read_cr3();

	strncpy(task->name, name, MAX_TASK_NAME_LEN);
	task->name[MAX_TASK_NAME_LEN - 1] = '\0';

	enable_preemption();
	return task;
}

void kthread_destroy(struct task* task)
{
	disable_preemption();

	// TODO: Free page tables
	// TODO: Free vm_mm

	// TODO: Do we need to make sure the list is initialized?
	list_del(&task->sched_list);
	free_pages((void*)task->kernel_stack, STACK_SIZE_PAGES);
	slab_free(squeue.cache, task);

	enable_preemption();
}

/**
 * kthread_run - Start execution of a kernel thread
 * @task: Pointer to the task structure representing the kernel thread
 *
 * Return: 0 on success, -1 if task has no entry point specified
 */
int kthread_run(struct task* task)
{
	if (!task->regs->rip) {
		log_error("Could not run kthread: no entry was specified");
		return -1;
	}

	disable_preemption();
	task->state = READY;
	__task_add(task);
	enable_preemption();

	return 0;
}

int launch_init()
{
	disable_preemption();

	// We are going to use kthread_create to bootstrap this.
	struct task* task = kthread_create("Init", nullptr);
	if (!task) {
		enable_preemption();
		return -1;
	}

	task->type = USER_TASK;
	task->pid = INIT_PID;
	vas_set_pml4(task->vas, (pgd_t*)vmm_create_address_space());

	static constexpr char init_path[] = "/usr/bin/init.elf";

	const char* argv[] = {
		init_path,
		"YOU_ARE_INIT",
		NULL,
	};

	const char* envp[] = {
		"PATH=/bin:/usr/bin:/usr/local/bin",
		"HOME=/home/user",
		"USER=user",
		"SHELL=/bin/bash",
		"TERM=xterm",
		"LANG=en_US.UTF-8",
		"PWD=/home/user/project",
		NULL,
	};

	struct exec_context* ctx = prepare_exec(init_path, argv, envp);
	if (!ctx) {
		kthread_destroy(task);
		enable_preemption();
		return -ENOENT;
	}

	// open /dev/console three times and pin them as 0,1,2
	int fd0 = __vfs_open_for_task(task, "/dev/console", O_RDONLY);
	int fd1 = __vfs_open_for_task(task, "/dev/console", O_WRONLY);
	int fd2 = __vfs_open_for_task(task, "/dev/console", O_WRONLY);
	// ensure they land at 0,1,2 even if earlier slots weren’t empty
	if (fd0 != 0) {
		__install_fd_at(task, task->resources[fd0], 0);
		task->resources[fd0] = NULL;
	}
	if (fd1 != 1) {
		__install_fd_at(task, task->resources[fd1], 1);
		task->resources[fd1] = NULL;
	}
	if (fd2 != 2) {
		__install_fd_at(task, task->resources[fd2], 2);
		task->resources[fd2] = NULL;
	}

	commit_exec(task, ctx);
	kthread_run(task);

	enable_preemption();
	return 0;
}

void reap_task(struct task* task)
{
	struct task* pos = nullptr;
	struct task* temp = nullptr;

	list_for_each_entry_safe(pos, temp, &squeue.terminated_list, sched_list)
	{
		log_info("Cleaning up task '%s' (PID %d)", pos->name, pos->pid);
		task_remove(pos);
		void* stack_base = (void*)(task->kernel_stack -
					   (STACK_SIZE_PAGES * PAGE_SIZE));
		free_pages(stack_base, STACK_SIZE_PAGES);

		free_page(pos->vas->pml4);

		list_del(&pos->sibling);
		slab_free(squeue.cache, pos);
	}
}

[[noreturn]]
void task_end(int status)
{
	struct task* task = get_current_task();
	log_info("Task '%s' (PID %d) exiting with status %d",
		 task->name,
		 task->pid,
		 status);

	address_space_destroy(task->vas);
	for (int i = 0; i < MAX_RESOURCES; i++) {
		if (task->resources[i]) {
			vfs_close(i);
		}
	}

	task->state = TERMINATED;
	list_move_tail(&task->sched_list, &squeue.terminated_list);

	task->exit_code = status;

	waitqueue_wake_one(&task->parent->parent_wq);

	yield();

	__builtin_unreachable();
}

int copy_thread_state(struct task* child, struct registers* parent_regs)
{
	void* stack = (void*)get_free_pages(AF_KERNEL, STACK_SIZE_PAGES);
	if (!stack) return -ENOMEM;

	uptr stack_top = (uptr)stack + (STACK_SIZE_PAGES * PAGE_SIZE);
	child->kernel_stack = stack_top;
	child->regs = (struct registers*)(uintptr_t)(stack_top -
						     sizeof(struct registers));

	memcpy(child->regs, parent_regs, sizeof(struct registers));

	return 0;
}

/**
 * __alloc_task - Allocate and initialize a new task structure
 * 
 * Return: Pointer to initialized task structure on success, nullptr on OOM
 */
struct task* __alloc_task()
{
	struct task* task = slab_alloc(squeue.cache);
	if (!task) {
		log_error("OOM error from slab_alloc");
		return nullptr;
	}
	struct address_space* vas = alloc_address_space();
	if (!vas) {
		slab_free(squeue.cache, task);
		return nullptr;
	}

	memset(task, 0, sizeof(struct task));
	task->vas = vas;
	// task->pid = squeue.pid_i++;

	// Init lists, maybe default resources (stdio)
	struct task* parent = get_current_task();
	task->parent = parent;

	list_init(&task->sched_list);
	list_init(&task->wait_list);
	list_init(&task->children);
	list_init(&task->sibling);
	waitqueue_init(&task->parent_wq);

	// The first task is its own parent
	if (task->parent != nullptr && task->parent != task) {
		list_add_tail(&task->parent->children, &task->sibling);
	}

	// Initialize stdio

	return task;
}

void task_wake(struct task* task)
{
	disable_preemption();

	if (task->state != BLOCKED) {
		enable_preemption();
		return;
	}

	task->state = READY;
	list_move_tail(&task->sched_list, &squeue.ready_list);

	enable_preemption();
}

void task_block(struct task* task)
{
	disable_preemption();

	__task_block(task, &squeue.blocked_list);
	// task->state = BLOCKED;
	// list_move_tail(&task->list, &squeue.blocked_list);

	enable_preemption();
}

/**
 * Handles the scheduler tick, decrementing sleep ticks for blocked tasks.
 */
void scheduler_tick()
{
	struct task* task = squeue.current_task;
	for (size_t i = 0; i < squeue.task_count; i++) {
		task = list_next_entry(task, sched_list);
		if (task->state == BLOCKED && task->sleep_ticks > 0) {
			task->sleep_ticks--;
			if (task->sleep_ticks == 0) task->state = READY;
		}
	}
}

void scheduler_dump()
{
	log_info("Scheduler Tasks");
	struct task* pos;
	log_info("Ready List:");
	list_for_each_entry (pos, &squeue.ready_list, sched_list) {
		log_info(
			"  %d: %s, type='%s', state=%d, kernel_stack=%lx, cr3=%lx",
			pos->pid,
			pos->name,
			get_task_name(pos->type),
			pos->state,
			pos->kernel_stack,
			pos->vas->pml4_phys);
	}
	log_info("Blocked List:");
	list_for_each_entry (pos, &squeue.blocked_list, sched_list) {
		log_info(
			"  %d: %s, type='%s', state=%d, kernel_stack=%lx, cr3=%lx",
			pos->pid,
			pos->name,
			get_task_name(pos->type),
			pos->state,
			pos->kernel_stack,
			pos->vas->pml4_phys);
	}
}

/**
 * Forces the current task to yield the CPU, triggering a reschedule.
 */
void yield()
{
	need_reschedule = true;
	__asm__ volatile("int $0x30"); // use an unused vector
}

/**
 * Blocks the current task and yields the CPU.
 */
void yield_blocked()
{
	task_block(squeue.current_task);
	yield();
}

void __wq_add_to_list(struct waitqueue* wqueue, struct task* task)
{
	if (list_empty(&task->wait_list)) {
		list_add_tail(&task->wait_list, &wqueue->waiters_list);
	} else {
		list_move_tail(&task->wait_list, &wqueue->waiters_list);
	}
}

void waitqueue_init(struct waitqueue* wqueue)
{
	if (!wqueue) return;
	list_init(&wqueue->waiters_list);
	spin_init(&wqueue->waiters_lock);
}

bool waitqueue_has_waiters(struct waitqueue* wqueue)
{
	if (!wqueue) return false;
	return !list_empty(&wqueue->waiters_list);
}

/**
 * Adds current task to waitqueue without blocking it.
 */
void waitqueue_prepare_wait(struct waitqueue* wqueue)
{
	unsigned long flags;
	spin_lock_irqsave(&wqueue->waiters_lock, &flags);

	struct task* task = get_current_task();
	task->wait_state = WAIT_PREPARING;
	task->wait = wqueue;
	// __wq_add_to_list(wqueue, task);
	list_add_tail(&task->wait_list, &wqueue->waiters_list);

	spin_unlock_irqrestore(&wqueue->waiters_lock, flags);
}

/**
 * Actually blocks the task
 */
void waitqueue_commit_sleep(struct waitqueue* wqueue)
{
	disable_preemption();

	struct task* task = get_current_task();

	unsigned long flags;
	spin_lock_irqsave(&wqueue->waiters_lock, &flags);

	if (task->wait_state == WAIT_WOKEN) {
		list_del(&task->wait_list);
		task->wait_state = WAIT_NONE;
		task->wait = nullptr;
		spin_unlock_irqrestore(&wqueue->waiters_lock, flags);
		enable_preemption();
		return;
	}

	__task_block(task, &squeue.blocked_list);
	task->wait_state = WAIT_SLEEPING;
	task->wait = wqueue;

	spin_unlock_irqrestore(&wqueue->waiters_lock, flags);
	enable_preemption();
	yield();
}

/**
 * Removes from waitqueue without blocking
 */
void waitqueue_cancel_wait(struct waitqueue* wqueue)
{
	unsigned long flags;
	spin_lock_irqsave(&wqueue->waiters_lock, &flags);

	struct task* task = get_current_task();

	list_del(&task->wait_list);
	task->wait_state = WAIT_NONE;
	task->wait = nullptr;

	spin_unlock_irqrestore(&wqueue->waiters_lock, flags);
}

void waitqueue_sleep(struct waitqueue* wqueue)
{
	waitqueue_prepare_wait(wqueue);
	waitqueue_commit_sleep(wqueue);
}

void waitqueue_wake_one(struct waitqueue* wqueue)
{
	if (!wqueue) return;

	ulong flags;
	spin_lock_irqsave(&wqueue->waiters_lock, &flags);

	if (list_empty(&wqueue->waiters_list)) {
		goto cleanup;
	}

	struct task* next =
		list_first_entry(&wqueue->waiters_list, struct task, wait_list);

	switch (next->wait_state) {
	case WAIT_PREPARING:
		next->wait_state = WAIT_WOKEN;
		// list_del(&next->wait_list);
		goto cleanup;
	case WAIT_SLEEPING:
		next->wait = nullptr;
		next->wait_state = WAIT_NONE;
		list_del(&next->wait_list);
		task_wake(next);
		goto cleanup;
	case WAIT_WOKEN:
	case WAIT_NONE:
	}

cleanup:
	spin_unlock_irqrestore(&wqueue->waiters_lock, flags);
}

void waitqueue_wake_all(struct waitqueue* wqueue)
{
	if (!wqueue) return;

	panic("wake all");

	// disable_preemption();

	ulong flags;
	spin_lock_irqsave(&wqueue->waiters_lock, &flags);

	struct task* pos = nullptr;
	struct task* temp = nullptr;
	list_for_each_entry_safe(pos, temp, &wqueue->waiters_list, wait_list)
	{
		pos->wait = nullptr;
		pos->wait_state = WAIT_NONE;
		list_del(&pos->wait_list);
		task_wake(pos);
	}

	spin_unlock_irqrestore(&wqueue->waiters_lock, flags);
	// enable_preemption();
}

void waitqueue_dump_waiters(struct waitqueue* wqueue)
{
	struct task* pos = nullptr;
	list_for_each_entry (pos, &wqueue->waiters_list, wait_list) {
		log_info(
			"  %d: %s, type='%s', state=%d, kernel_stack=%lx, cr3=%lx",
			pos->pid,
			pos->name,
			get_task_name(pos->type),
			pos->state,
			pos->kernel_stack,
			pos->vas->pml4_phys);
	}
}

/**
 * __install_fd_at - Install @file at a specific descriptor number.
 * @t:   target task
 * @fd:  descriptor index (e.g., 0,1,2)
 * Return: 0 on success, -1 if @fd is out of range or occupied.
 */
int __install_fd_at(struct task* t, struct vfs_file* file, int fd)
{
	if (fd < 0 || fd >= MAX_RESOURCES) return -1;
	if (t->resources[fd] != nullptr) return -1;
	t->resources[fd] = file;
	return 0;
}

int install_fd(struct task* t, struct vfs_file* file)
{
	for (size_t i = 0; i < MAX_RESOURCES; i++) {
		if (t->resources[i] == nullptr) {
			t->resources[i] = file;
			return (int)i;
		}
	}
	return -1;
}

void __task_add(struct task* task)
{
	log_debug("Appending new task to list");
	list_add_tail(&squeue.ready_list, &task->sched_list);
	squeue.task_count++;

	log_debug("Added task %d", task->pid);
	log_debug("Currently have %lu tasks", squeue.task_count);
}

/*******************************************************************************
* Private Function Definitions
*******************************************************************************/

static struct task* pick_next()
{
	if (list_empty(&squeue.ready_list)) {
		// No ready task available
		squeue.current_task = idle_task;
		return squeue.current_task;
	}

	// If we only have 1 task then might as well make sure we continue it
	if (squeue.task_count == 1) {
		return squeue.current_task;
	}

	struct task* current = get_current_task();
	struct task* next;

	// TODO: Skip over WAIT_PREPARING tasks

	// Since we have a blocked list now, we can assume that everything in the
	// ready list is READY. So we don't have to do any looping for a simple
	// round-robin scheduler. (This still sucks though)
	if (current->state != READY) {
		next = list_first_entry(
			&squeue.ready_list, struct task, sched_list);
	} else {
		next = list_next_entry_circular(
			current, &squeue.ready_list, sched_list);
	}

	squeue.current_task = next;
	return next;
}

static int create_kernel_stack(struct task* task, entry_func entry)
{
	void* stack = (void*)get_free_pages(AF_KERNEL, STACK_SIZE_PAGES);
	log_debug("Allocated stack at %p", stack);
	if (!stack) return -ENOMEM;

	uintptr_t stack_top = (uintptr_t)stack + STACK_SIZE_PAGES * PAGE_SIZE;

	task->kernel_stack = stack_top;
	task->regs = (struct registers*)(uintptr_t)(stack_top -
						    sizeof(struct registers));
	// Simulate interrupt frame
	task->regs->ss = KERNEL_DS; // optional for ring 0
	task->regs->rsp = stack_top;
	task->regs->rflags = 0x202;
	task->regs->cs = KERNEL_CS; // kernel code segment

	// Other important registers, all other registers set to 0
	task->regs->ds = KERNEL_DS;
	task->regs->saved_rflags = 0x202;

	// A null entry doesn't matter as long as we are not doing a first run
	// with a null entry
	task->regs->rip = (uptr)entry;

	log_debug("Created stack for task %d, kernel_stack: %lx, regs addr: %p",
		  task->pid,
		  task->kernel_stack,
		  (void*)task->regs);

	return 0;
}

static void task_remove(struct task* task)
{
	list_del(&task->sched_list);
	squeue.task_count--;
}

static void idle_task_entry()
{
	while (1) {
		halt();
	}
}

/**
 * setup_first_kernel_task - Initialize the primary kernel task
 * 
 * Creates and configures the initial kernel task that represents the kernel's
 * execution context. This task serves as the root of the task hierarchy and
 * is used for kernel-level operations that require a task context.
 * 
 * This function must be called during early kernel initialization before
 * any other tasks are created or scheduled.
 */
static void setup_first_kernel_task()
{
	kernel_task = __alloc_task();
	if (!kernel_task) {
		panic("Unable to allocate initial kernel task");
	}

	kernel_task->type = KERNEL_TASK;
	kernel_task->parent = kernel_task;
	kernel_task->pid = squeue.kernel_pid_counter++;

	// Set kernel_stack to stack we set from __arch_entry
	extern void* g_entry_new_stack;
	kernel_task->kernel_stack = (uptr)g_entry_new_stack;

	vas_set_pml4(kernel_task->vas, (pgd_t*)PHYS_TO_HHDM(vmm_read_cr3()));

	strncpy(kernel_task->name, "Kernel Task", MAX_TASK_NAME_LEN);
	kernel_task->name[MAX_TASK_NAME_LEN - 1] = '\0';

	kernel_task->state = RUNNING;
	__task_add(kernel_task);
	squeue.current_task = kernel_task;
}

/**
 * setup_idle_task - Create the idle task for CPU idle periods
 * 
 * This task is selected by the scheduler when the ready queue is empty.
 */
static void setup_idle_task()
{
	idle_task = kthread_create("Idle task", (entry_func)idle_task_entry);
	// kthread_run(idle_task);

	// idle_task->parent = kernel_task;
	// idle_task->state = IDLE;
}
