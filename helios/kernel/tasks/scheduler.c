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
#define LOG_LEVEL 1
#define FORCE_LOG_REDEF
#include <util/log.h>
#undef FORCE_LOG_REDEF

#include <stdlib.h>
#include <string.h>

#include <arch/gdt/gdt.h>
#include <arch/idt.h>
#include <arch/mmu/vmm.h>
#include <arch/regs.h>
#include <kernel/panic.h>
#include <kernel/tasks/scheduler.h>
#include <mm/page.h>
#include <mm/page_alloc.h>
#include <mm/slab.h>
#include <util/list.h>

/*******************************************************************************
* Global Variable Definitions
*******************************************************************************/

volatile bool need_reschedule = false;
// If > 0, preempt is disabled
static volatile int preempt_count = 1;

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

/**
 * @brief Adds a task to the scheduler queue.
 *
 * @param task Pointer to the task structure to add.
 */
static void task_add(struct task* task);

/**
 * @brief Creates a kernel stack an imitates an interrupt frame for the given task.
 *
 * @param task Pointer to the task structure.
 * @return 0 on success, 1 on failure.
 */
static int create_kernel_stack(struct task* task);

/**
 * Entry point for the idle task. This task halts the CPU in an infinite loop.
 */
static void idle_task_entry();

/**
 * Checks whether preemption is enabled by checking the preemption counter.
 */
static inline bool is_preempt_enabled()
{
	return preempt_count == 0;
}

/*******************************************************************************
* Public Function Definitions
*******************************************************************************/

/**
 * Enables task preemption by decrementing the preemption counter.
 */
void enable_preemption(void)
{
	preempt_count--;
}

/**
 * Disables task preemption by incrementing the preemption counter.
 * If the counter underflows, a panic is triggered.
 */
void disable_preemption(void)
{
	if (++preempt_count < 0) panic("preempt count underflow");
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

/**
 * Checks if a reschedule is needed and performs a context switch if required.
 *
 * @param regs Pointer to the current task's register state.
 */
void schedule(struct registers* regs)
{
	if (!need_reschedule || !is_preempt_enabled()) return;

	need_reschedule = false;

	struct task* prev = squeue.current_task;
	prev->regs = regs;
	if (prev->state != BLOCKED) prev->state = READY;

	struct task* new = pick_next();
	if (new == NULL) {
		prev->state = RUNNING;
		return;
	}

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
	squeue.cache = kmalloc(sizeof(struct slab_cache));
	if (!squeue.cache) {
		log_error("OOM error from kmalloc");
		panic("OOM error from kmalloc");
	}
	list_init(&squeue.task_list);

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

	idle_task = new_task("Idle task", (entry_func)idle_task_entry);
	idle_task->cr3 = vmm_read_cr3();
	idle_task->parent = kernel_task;
	idle_task->state = IDLE;

	kernel_task = new_task("Kernel task", NULL);
	kernel_task->cr3 = vmm_read_cr3();
	kernel_task->parent = kernel_task;
	squeue.current_task = kernel_task;
	log_debug("Probably inited the scheduler");
	enable_preemption();
}

/**
 * Creates a new task with the given entry point.
 *
 * @param entry Pointer to the entry function for the task.
 * @return Pointer to the newly created task structure, or NULL on failure.
 */
struct task* new_task(const char* name, entry_func entry)
{
	disable_preemption();
	struct task* task = kmalloc(sizeof(struct task));
	if (task == NULL) return NULL;
	memset(task, 0, sizeof(struct task));

	create_kernel_stack(task);
	task->PID = squeue.pid_i++;
	task->cr3 = HHDM_TO_PHYS((uptr)vmm_create_address_space());
	// task->cr3    = vmm_read_cr3();
	task->parent = kernel_task;
	if (entry) {
		task->entry = entry;
		task->regs->rip = (uintptr_t)entry;
		// We only set the task to ready if we have an entry point
		// otherwise we wait until execve() is called
		task->state = READY;
	}

	strncpy(task->name, name, MAX_TASK_NAME_LEN);
	task->name[MAX_TASK_NAME_LEN - 1] = '\0';

	task_add(task);

	enable_preemption();
	return task;
}

/**
 * Handles the scheduler tick, decrementing sleep ticks for blocked tasks.
 */
void scheduler_tick()
{
	struct task* task = squeue.current_task;
	for (size_t i = 0; i < squeue.task_count; i++) {
		task = list_next_entry(task, list);
		if (task->state == BLOCKED && task->sleep_ticks > 0) {
			task->sleep_ticks--;
			if (task->sleep_ticks == 0) task->state = READY;
		}
	}
}

void scheduler_dump()
{
	log_info("Scheduler Tasks:");
	struct task* pos;
	list_for_each_entry (pos, &squeue.task_list, list) {
		log_info("  %lu: %s, type='%s', state=%d",
			 pos->PID,
			 pos->name,
			 get_task_name(pos->type),
			 pos->state);
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
	squeue.current_task->state = BLOCKED;
	yield();
}

void waitqueue_sleep(struct waitqueue* wqueue)
{
	struct task* t = get_current_task();
	t->state = BLOCKED;
	list_append(&wqueue->list, &t->list);
	yield();
}

void waitqueue_wake_one(struct waitqueue* wqueue)
{
	if (!wqueue) return;
	struct task* head = list_head(&wqueue->list, struct task, list);
	log_debug("Waking task %lu", head->PID);
	head->state = READY;
	list_remove(&head->list);
}

void waitqueue_wake_all(struct waitqueue* wqueue)
{
	if (!wqueue) return;
	struct task* pos;
	list_for_each_entry (pos, &wqueue->list, list) {
		log_debug("Waking task %lu", pos->PID);
		pos->state = READY;
		list_remove(&pos->list);
	}
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

/*******************************************************************************
* Private Function Definitions
*******************************************************************************/

static struct task* pick_next()
{
	if (list_empty(&squeue.task_list)) return NULL;
	// If we only have 1 task then might as well make sure we continue it
	if (squeue.task_count == 1) return squeue.current_task;

	struct task* t = squeue.current_task;
	for (size_t i = 0; i < squeue.task_count; i++) {
		t = list_next_entry(t, list);
		if (t->state == READY) {
			squeue.current_task = t;
			return t;
		}
	}
	// No ready task found
	squeue.current_task = idle_task;
	return squeue.current_task;
}

static int create_kernel_stack(struct task* task)
{
	constexpr size_t STACK_SIZE_PAGES = 1;
	void* stack = (void*)get_free_pages(AF_KERNEL, STACK_SIZE_PAGES);
	log_debug("Allocated stack at %p", stack);
	if (!stack) return -EOOM;

	uintptr_t stack_top = (uintptr_t)stack + STACK_SIZE_PAGES * PAGE_SIZE;

	task->kernel_stack = stack_top;
	task->regs = (struct registers*)(uintptr_t)(stack_top -
						    sizeof(struct registers));
	// Simulate interrupt frame
	task->regs->ss = 0x10; // optional for ring 0
	task->regs->rsp = stack_top;
	task->regs->rflags = 0x202;
	task->regs->cs = KERNEL_CS; // kernel code segment

	// Other important registers, all other registers set to 0
	task->regs->ds = 0x10;
	task->regs->saved_rflags = 0x202;

	log_debug(
		"Created stack for task %lu, kernel_stack: %lx, regs addr: %p",
		task->PID,
		task->kernel_stack,
		(void*)task->regs);

	return 0;
}

static void task_add(struct task* task)
{
	log_debug("Appending new task to list");
	list_append(&squeue.task_list, &task->list);
	squeue.task_count++;

	log_debug("Added task %lu", task->PID);
	log_debug("Currently have %lu tasks", squeue.task_count);
}

static void idle_task_entry()
{
	while (1)
		halt();
}
