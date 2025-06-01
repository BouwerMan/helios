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

#include <string.h>

#include <kernel/liballoc.h>
#include <kernel/memory/slab.h>
#include <kernel/sys.h>
#include <kernel/tasks/scheduler.h>
#include <mm/page_alloc.h>
#include <util/list.h>
#include <util/log.h>

#include "../../arch/x86_64/interrupts/idt.h"

volatile bool need_reschedule = false;
// If > 0, preempt is disabled
static volatile int preempt_count = 1;

struct scheduler_queue squeue = { 0 };
struct task* kernel_task;
struct task* idle_task;

[[noreturn]] extern void __switch_to(struct registers* new);

#define preempt_enabled() (preempt_count == 0)

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
void check_reschedule(struct registers* regs)
{
	if (need_reschedule && preempt_enabled()) {
		need_reschedule = false;

		// TODO: I'm doing some weird shit here, make this cleaner
		// TODO: Load new cr3
		squeue.current_task->regs = regs;
		if (squeue.current_task->state != BLOCKED) squeue.current_task->state = READY;

		struct task* new = scheduler_pick_next();
		if (new == NULL) {
			squeue.current_task->state = RUNNING;
			return;
		}

		new->state = RUNNING;

		/* @ref Does not return */
		__switch_to(new->regs);
		__builtin_unreachable();
	}
}

#define STACK_SIZE_PAGES 1

/**
 * Creates a kernel stack an imitates an interrupt frame for the given task.
 *
 * @param task Pointer to the task structure.
 * @return 0 on success, 1 on failure.
 */
static int create_stack(struct task* task)
{
	// TODO: Allocate in userspace if needed
	void* stack = (void*)get_free_pages(0, STACK_SIZE_PAGES);
	if (!stack) return -EOOM;
	memset(stack, 0, STACK_SIZE_PAGES * PAGE_SIZE);

	uintptr_t stack_top = (uintptr_t)stack;

	task->kernel_stack = stack_top;
	task->regs = (struct registers*)(uintptr_t)(stack_top - sizeof(struct registers));
	// Simulate interrupt frame
	task->regs->ss = 0x10; // optional for ring 0
	task->regs->rsp = stack_top;
	task->regs->rflags = 0x202;
	task->regs->cs = 0x08; // kernel code segment

	// Other important registers, all other registers set to 0
	task->regs->ds = 0x10;
	task->regs->saved_rflags = 0x202;

	log_debug("Created stack for task %lu, kernel_stack: %lx, regs addr: %p", task->PID, task->kernel_stack,
		  (void*)task->regs);

	return 0;
}

/**
 * Adds a task to the scheduler queue.
 *
 * @param task Pointer to the task structure to add.
 */
void task_add(struct task* task)
{
	log_debug("Appending new task to list");
	list_append(&squeue.task_list, &task->list);
	squeue.task_count++;

	log_debug("Added task %lu", task->PID);
	log_debug("Currently have %lu tasks", squeue.task_count);
}

/**
 * Entry point for the idle task. This task halts the CPU in an infinite loop.
 */
static void idle_task_entry()
{
	while (1)
		halt();
}

/**
 * Initializes the scheduler by creating the idle and kernel tasks.
 */
void init_scheduler(void)
{
	squeue.cache = kmalloc(sizeof(struct slab_cache));
	if (!squeue.cache) {
		log_error("OOM error from kmalloc");
		panic("OOM error from kmalloc");
	}
	list_init(&squeue.task_list);

	int res = slab_cache_init(squeue.cache, "Scheduler Tasks", sizeof(struct task), 0, NULL, NULL);
	if (res < 0) {
		log_error("Could not init scheduler tasks cache, slab_cache_init() returned %d", res);
		panic("Scheduler tasks cache init failure");
	}

	idle_task = new_task((entry_func)idle_task_entry);
	idle_task->cr3 = vmm_read_cr3();
	idle_task->parent = kernel_task;
	idle_task->state = IDLE;

	kernel_task = new_task(NULL);
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
struct task* new_task(entry_func entry)
{
	struct task* task = kmalloc(sizeof(struct task));
	if (task == NULL) return NULL;
	memset(task, 0, sizeof(struct task));

	create_stack(task);
	task->PID = squeue.pid_i++;
	task->cr3 = vmm_read_cr3();
	task->parent = kernel_task;
	if (entry) {
		task->entry = entry;
		task->regs->rip = (uintptr_t)entry;
	}
	task->state = READY;
	task_add(task);

	return task;
}

/**
 * Picks the next task to run from the scheduler queue.
 *
 * @return Pointer to the next task structure, or the idle task if no ready task is found.
 */
struct task* scheduler_pick_next()
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
	list_for_each_entry(pos, &wqueue->list, list)
	{
		log_debug("Waking task %lu", pos->PID);
		pos->state = READY;
		list_remove(&pos->list);
	}
}
