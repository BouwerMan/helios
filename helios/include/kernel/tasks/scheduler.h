/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <mm/address_space.h>
#include <stddef.h>
#include <stdint.h>
#include <util/list.h>

typedef void (*entry_func)(void);

static constexpr size_t STACK_SIZE_PAGES = 32;
static constexpr int MAX_TASK_NAME_LEN = 32;

static constexpr int SCHEDULER_TIME = 20; // ms per preemptive tick
static constexpr int MAX_RESOURCES = 128;

enum TASK_STATE {
	INITIALIZED,
	BLOCKED,
	READY,
	RUNNING,
	IDLE,
	TERMINATED,
};

enum TASK_TYPE {
	KERNEL_TASK,
	USER_TASK,
};

static const char* task_type_names[] = {
	"Kernel Task",
	"User Task",
};

static inline const char* get_task_name(enum TASK_TYPE type)
{
	if (type < 0) type = -type;
	return task_type_names[type];
}

struct waitqueue {
	struct list_head waiters_list;
	spinlock_t waiters_lock;
};

// Any changes to this structure needs to be reflected in switch.asm
// This represents ANY schedulable task
struct task {
	struct registers*
		regs; // Full CPU context, this address is loaded into rsp on switch
	struct address_space* vas;
	uintptr_t kernel_stack; // The top of the kernel stack
	enum TASK_STATE state;
	enum TASK_TYPE type;
	uint8_t priority;
	int preempt_count;
	pid_t pid;
	volatile uint64_t sleep_ticks;
	struct vfs_file* resources[MAX_RESOURCES];
	struct task* parent;	    // Should this just be parent PID?

	struct list_head list;	    // For the scheduler's main task list
	struct list_head children;  // Head of this task's children list
	struct list_head sibling;   // Node in the parent's children list
	int exit_code;		    // Store the exit code
	struct waitqueue parent_wq; // For parents to wait on

	struct waitqueue*
		wait; // For debugging, stores current blocking waitqueue

	char name[MAX_TASK_NAME_LEN];
};

struct scheduler_queue {
	struct list_head ready_list; // list head of the queue
	struct list_head blocked_list;
	struct list_head terminated_list;
	struct task* current_task;
	struct slab_cache* cache;
	size_t task_count;
	pid_t pid_i;
	bool inited;
};

static inline bool waitqueue_empty(struct waitqueue* wqueue)
{
	return list_empty(&wqueue->waiters_list);
}

/**
 * __alloc_task - Allocate and initialize a new task structure
 * 
 * Return: Pointer to initialized task structure on success, nullptr on OOM
 */
struct task* __alloc_task();
int copy_thread_state(struct task* child, struct registers* parent_regs);
int launch_init();

int kthread_run(struct task* task);
struct task* kthread_create(const char* name, entry_func entry);
void kthread_destroy(struct task* task);
struct task* new_task(const char* name, entry_func entry);
void schedule(struct registers* regs);
void scheduler_init(void);
bool is_scheduler_init();
void scheduler_tick();
void enable_preemption();
void disable_preemption();
void yield();
void yield_blocked();
void task_wake(struct task* task);
void task_block(struct task* task);
void reap_task(struct task* task);

[[noreturn]]
void task_end(int status);

struct task* get_current_task();
struct scheduler_queue* get_scheduler_queue();

void scheduler_dump();

/// Waitqueue

void waitqueue_init(struct waitqueue* wqueue);
void waitqueue_sleep(struct waitqueue* wqueue);
void waitqueue_sleep_unlock(struct waitqueue* wqueue,
			    spinlock_t* lock,
			    unsigned long flags);
void waitqueue_wake_one(struct waitqueue* wqueue);
void waitqueue_wake_all(struct waitqueue* wqueue);
void waitqueue_dump_waiters(struct waitqueue* wqueue);

int install_fd(struct task* t, struct vfs_file* file);

/**
 * @brief Adds a task to the scheduler queue.
 *
 * @param task Pointer to the task structure to add.
 */
void __task_add(struct task* task);
