#include <kernel/liballoc.h>
#include <kernel/memory/pmm.h>
#include <kernel/memory/vmm.h>
#include <kernel/sys.h>
#include <kernel/tasks/scheduler.h>
#include <string.h>
#include <util/log.h>

volatile bool need_reschedule = false;
// If > 0, preempt is disabled
static volatile int preempt_count = 1;

struct scheduler_queue queue = { 0 };

extern void __switch_to(struct registers* new);

#define preempt_enabled() (preempt_count == 0)

void enable_preemption(void)
{
	preempt_count--;
}

void disable_preemption(void)
{
	if (++preempt_count < 0) panic("preempt count underflow");
}

void check_reschedule(struct registers* regs)
{
	if (need_reschedule && preempt_enabled()) {
		need_reschedule = false;

		// TODO: I'm doing some weird shit here, make this cleaner
		queue.current_task->regs = regs;
		queue.current_task->state = READY;

		struct task* new = scheduler_pick_next();
		if (new == NULL) return;
		if (new->state == INITIALIZED) {
			new->regs->rip = (uintptr_t)new->entry;
		}

		new->state = RUNNING;

		__switch_to(new->regs);
	}
}

#define STACK_SIZE_PAGES 1

int create_stack(struct task* task)
{
	void* stack = vmm_alloc_pages(1, false);
	if (!stack) return 1;
	memset(stack, 0, STACK_SIZE_PAGES * PAGE_SIZE);

	uintptr_t stack_top = (uintptr_t)stack;
	uintptr_t* sp = stack;
	// All of this simulates a PUSHALL and interrupt
	// Top of stack grows downwards
	*--sp = 0x10;		// SS  ← optional for ring 0
	*--sp = stack_top;	// RSP
	*--sp = 0x202;		// RFLAGS (interrupts enabled)
	*--sp = 0x08;		// CS (kernel code segment)
	*--sp = (uintptr_t)0x0; // RIP, initially 0, gets added by another function

	sp -= 2; // Space for irq and error code

	// PUSHALL
	*--sp = 0x202; // pushfq (optional, if you do popfq)

	// TODO: Make this better
	*--sp = 0x0; // r15
	*--sp = 0x0; // r14
	*--sp = 0x0; // r13
	*--sp = 0x0; // r12
	*--sp = 0x0; // r11
	*--sp = 0x0; // r10
	*--sp = 0x0; // r9
	*--sp = 0x0; // r8

	*--sp = 0x0; // rax
	*--sp = 0x0; // rcx
	*--sp = 0x0; // rdx
	*--sp = 0x0; // rbx
	*--sp = 0x0; // rsp
	*--sp = 0x0; // rbp
	*--sp = 0x0; // rsi
	*--sp = 0x0; // rdi

	*--sp = 0x10; // ds (?)

	task->kernel_stack = (uintptr_t)stack;
	task->regs = (struct registers*)sp;

	log_debug("Created stack for task %d, kernel_stack: %lx, regs addr: %p", task->PID, task->kernel_stack,
		  (void*)task->regs);

	return 0;
}

struct task* task_add(void)
{
	struct task* task = kmalloc(sizeof(struct task));
	if (task == NULL) return NULL;
	memset(task, 0, sizeof(struct task));

	task->PID = queue.pid_i++;
	task->state = UNREADY;
	create_stack(task);
	if (queue.list == 0) {
		log_debug("Initializing new list");
		list_init(&task->list);
		queue.list = &task->list;
	} else {
		log_debug("Appending new task to list");
		list_append(queue.list, &task->list);
	}

	log_debug("Added task %d", task->PID);
	return task;
}

void init_scheduler(void)
{
	struct task* kernel_task = task_add();
	kernel_task->cr3 = vmm_read_cr3();
	kernel_task->parent = kernel_task;
	queue.current_task = kernel_task;
	log_debug("Probably inited the scheduler");
	enable_preemption();
}

/// Returns NULL if invalid next task
/// Sets the current task
struct task* scheduler_pick_next()
{
	if (queue.list == NULL) return NULL;
	struct task* next = list_entry(queue.current_task->list.next, struct task, list);
	// If the next is not ready, we stay with the current task
	while (next->state != READY) {
		next = list_entry(next->list.next, struct task, list);
		// If we looped the list we resume same task
		if (next == queue.current_task) break;
	}
	queue.current_task = next;
	return queue.current_task;
}
