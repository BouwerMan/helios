#pragma once

// TODO: Gotta be a better way for me to do this lmao
#include "../../../arch/x86_64/interrupts/idt.h"

#include <drivers/fs/vfs.h>
#include <stdint.h>

#define MAX_RESOURCES 20

enum TASK_STATE {
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
	struct vfs_file* resources[MAX_RESOURCES];
	struct task* parent; // Should this just be parent PID?
	struct task* next;
};

void context_switch(struct task* current, struct task* next);
