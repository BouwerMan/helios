#pragma once

// TODO: Gotta be a better way for me to do this lmao
#include "../../../arch/x86_64/interrupts/idt.h"

#include <drivers/fs/vfs.h>
#include <stdint.h>
#include <util/list.h>

#define SCHEDULER_TIME 20 // ms per preemptive tick
#define MAX_RESOURCES  20

enum TASK_STATE {
	UNREADY,
	INITIALIZED,
	BLOCKED,
	READY,
	RUNNING,
};

struct task {
	struct registers* regs;
	uintptr_t cr3;		// pml4
	uintptr_t kernel_stack; // Not super sure abt this one
	enum TASK_STATE state;
	uint8_t priority;
	uint8_t PID;
	void* entry;
	struct vfs_file* resources[MAX_RESOURCES];
	struct task* parent; // Should this just be parent PID?
	struct list list;
};

struct scheduler_queue {
	struct list* list; // list head of the queue
	struct task* current_task;
	uint64_t pid_i;
};

struct task* task_add(void);
void check_reschedule(struct registers* regs);
void init_scheduler(void);
struct task* scheduler_pick_next();
void enable_preemption();
void disable_preemption();
