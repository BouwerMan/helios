/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "kernel/spinlock.h"
#include "lib/list.h"

/**
 * @addtogroup sched
 * @{
 */

typedef void (*entry_func)(void);

// Negative PIDs for kernel threads
static constexpr pid_t KERNEL_PID_BASE = -1000;
// Reserved for init
static constexpr pid_t INIT_PID = 1;
// Start user processes from 2
static constexpr pid_t USER_PID_BASE = 2;

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

enum TASK_WAIT_STATE {
	WAIT_NONE,	// Not waiting on anything
	WAIT_PREPARING, // Added to waitqueue but not sleeping yet
	WAIT_SLEEPING,	// Actually blocked and sleeping
	WAIT_WOKEN,	// Woken during prepare phase
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

/**
 * @brief A set of tasks blocked on the same event.
 */
struct waitqueue {
	struct list_head waiters_list; /**< List of blocked tasks. */
	spinlock_t waiters_lock;       /**< Locks waiters_list. */
};

/**
 * @brief Represents any schedulable task, kernel or user.
 *
 * @warning switch.asm reads this structure directly. Reflect any layout
 * change there too.
 */
struct task {
	/**
	 * @brief Full CPU register context.
	 *
	 * This address is loaded into rsp on a context switch.
	 */
	struct registers* regs;
	struct address_space* vas;	 /**< The task's virtual address space. */
	uintptr_t kernel_stack;		 /**< Top of the kernel stack. */
	enum TASK_STATE state;		 /**< Current scheduler state. */
	enum TASK_WAIT_STATE wait_state; /**< Current waitqueue state. */
	enum TASK_TYPE type;		 /**< Kernel task or user task. */
	uint8_t priority;		 /**< Scheduling priority. */
	int preempt_count;		 /**< Preemption-disable nesting count. Zero means preemption is allowed. */
	pid_t pid;			 /**< Process ID. */
	uint64_t sleep_ticks;		 /**< Remaining ticks to sleep. */
	int errno_value;		 /**< The task's current errno value. */
	struct vfs_dentry* cwd;		 /**< Current working directory. */
	struct vfs_file* resources[MAX_RESOURCES]; /**< Open file table, indexed by file descriptor. */
	struct task* parent;			   /**< Parent task. */

	struct list_head sched_list;		   /**< Node in the scheduler's main task list. */
	struct list_head wait_list;		   /**< Node in a waitqueue's waiter list. */
	struct list_head children;		   /**< Head of this task's children list. */
	struct list_head sibling;		   /**< Node in the parent's children list. */
	int exit_code;				   /**< Exit code. Valid once the task terminates. */
	struct waitqueue parent_wq;		   /**< Waitqueue the parent waits on for this task to exit. */

	char name[MAX_TASK_NAME_LEN];		   /**< Task name, null-terminated. */

	struct waitqueue* wait;			   /**< Waitqueue this task is blocked on. For debugging only. */
};

/**
 * @brief Per-CPU scheduler state: task lists, the running task, and PID
 * counters.
 */
struct scheduler_queue {
	/**
	 * @brief Set when a reschedule is needed.
	 *
	 * @warning Must stay the first field; the interrupt handler accesses
	 * it directly.
	 */
	volatile bool need_reschedule;
	spinlock_t lock;		  /**< Locks ready_list, blocked_list, and terminated_list. */
	struct list_head ready_list;	  /**< Tasks ready to run. */
	struct list_head blocked_list;	  /**< Tasks blocked on a waitqueue. */
	struct list_head terminated_list; /**< Tasks that have exited but not been reaped. */
	struct task* current_task;	  /**< Task currently running on this CPU. */

	struct task* idle_task;		  /**< Run when ready_list is empty. */

	struct slab_cache* cache;	  /**< Slab cache used to allocate struct task. */
	size_t task_count;		  /**< Number of tasks tracked by this queue. */
	pid_t kernel_pid_counter;	  /**< Next PID to assign to a kernel task. */
	pid_t user_pid_counter;		  /**< Next PID to assign to a user task. */
	bool inited;			  /**< True once the scheduler has been initialized. */

	struct task* last_task;		  /**< Most recently scheduled task. For debugging only. */
};

static inline bool waitqueue_empty(struct waitqueue* wqueue)
{
	return list_empty(&wqueue->waiters_list);
}

/**
 * @brief Allocates and initializes a new task structure.
 *
 * @return A pointer to the initialized task structure on success, or
 * nullptr on OOM.
 */
struct task* __alloc_task();
int copy_thread_state(struct task* child, struct registers* parent_regs);
int launch_init();

int kthread_run(struct task* task);
[[nodiscard]]
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
void sleep(u64 millis);

[[noreturn]]
void task_end(int status);

struct task* get_current_task();
struct scheduler_queue* get_scheduler_queue();

void scheduler_dump();

/// Waitqueue

void waitqueue_init(struct waitqueue* wqueue);
bool waitqueue_has_waiters(struct waitqueue* wqueue);
void waitqueue_prepare_wait(struct waitqueue* wqueue);
void waitqueue_commit_sleep(struct waitqueue* wqueue);
void waitqueue_cancel_wait(struct waitqueue* wqueue);
void waitqueue_sleep(struct waitqueue* wqueue);
void waitqueue_sleep_unlock(struct waitqueue* wqueue, spinlock_t* lock, unsigned long flags);
void waitqueue_wake_one(struct waitqueue* wqueue);
void waitqueue_wake_all(struct waitqueue* wqueue);
void waitqueue_dump_waiters(struct waitqueue* wqueue);

int __install_fd_at(struct task* t, struct vfs_file* file, int fd);
int install_fd(struct task* t, struct vfs_file* file);

/**
 * @brief Adds a task to the scheduler queue.
 *
 * @param task Pointer to the task structure to add.
 */
void __task_add(struct task* task);

/** @} */
