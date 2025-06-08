/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <util/list.h>

#define SCHEDULER_TIME 20 // ms per preemptive tick
#define MAX_RESOURCES  20

enum TASK_STATE {
	INITIALIZED,
	BLOCKED,
	READY,
	RUNNING,
	IDLE,
};

enum TASK_TYPE {
	KERNEL_TASK,
	USER_TASK,
};

typedef void (*entry_func)(void);

struct task {
	struct registers* regs;
	uintptr_t cr3;		// pml4
	uintptr_t kernel_stack; // Not super sure abt this one
	enum TASK_STATE state;
	enum TASK_TYPE type;
	uint8_t priority;
	uint64_t PID;
	volatile uint64_t sleep_ticks;
	entry_func entry;
	struct vfs_file* resources[MAX_RESOURCES];
	struct task* parent; // Should this just be parent PID?
	struct list list;
};

struct scheduler_queue {
	struct list task_list; // list head of the queue
	struct task* current_task;
	struct slab_cache* cache;
	size_t task_count;
	uint64_t pid_i;
};

struct waitqueue {
	struct list list;
};

void task_add(struct task* task);
struct task* new_task(entry_func entry);
void check_reschedule(struct registers* regs);
void init_scheduler(void);
struct task* scheduler_pick_next();
void scheduler_tick();
void enable_preemption();
void disable_preemption();
void yield();
void yield_blocked();

struct task* get_current_task();
struct scheduler_queue* get_scheduler_queue();

/// Waitqueue

void waitqueue_sleep(struct waitqueue* wqueue);
void waitqueue_wake_one(struct waitqueue* wqueue);
void waitqueue_wake_all(struct waitqueue* wqueue);
