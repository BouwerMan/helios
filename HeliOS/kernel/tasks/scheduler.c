#include <kernel/liballoc.h>
#include <kernel/memory/pmm.h>
#include <kernel/memory/vmm.h>
#include <kernel/sys.h>
#include <kernel/tasks/scheduler.h>
#include <string.h>
#include <util/list.h>
#include <util/log.h>

volatile bool need_reschedule = false;
// If > 0, preempt is disabled
static volatile int preempt_count = 1;

struct scheduler_queue queue = { 0 };
struct task* kernel_task;
struct task* idle_task;

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

struct task* get_current_task()
{
	return queue.current_task;
}

struct scheduler_queue* get_scheduler_queue()
{
	return &queue;
}

void check_reschedule(struct registers* regs)
{
	if (need_reschedule && preempt_enabled()) {
		need_reschedule = false;

		// TODO: I'm doing some weird shit here, make this cleaner
		// TODO: Load new cr3
		queue.current_task->regs = regs;
		if (queue.current_task->state != BLOCKED) queue.current_task->state = READY;

		struct task* new = scheduler_pick_next();
		if (new == NULL) {
			queue.current_task->state = RUNNING;
			return;
		}

		new->state = RUNNING;

		// Does not return
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

	log_debug("Created stack for task %d, kernel_stack: %lx, regs addr: %p", task->PID, task->kernel_stack,
		  (void*)task->regs);

	return 0;
}

/// Simply adds task to queue
void task_add(struct task* task)
{
	if (queue.list == 0) {
		log_debug("Initializing new list");
		list_init(&task->list);
		queue.list = &task->list;
	} else {
		log_debug("Appending new task to list");
		list_append(queue.list, &task->list);
	}
	queue.task_count++;

	log_debug("Added task %d", task->PID);
	log_debug("Currently have %lu tasks", queue.task_count);
}

void idle_task_entry()
{
	while (1)
		halt();
}

void init_scheduler(void)
{
	idle_task = new_task((void*)idle_task_entry);
	idle_task->cr3 = vmm_read_cr3();
	idle_task->parent = kernel_task;

	kernel_task = new_task(NULL);
	kernel_task->cr3 = vmm_read_cr3();
	kernel_task->parent = kernel_task;
	queue.current_task = kernel_task;
	log_debug("Probably inited the scheduler");
	enable_preemption();
}

struct task* new_task(void* entry)
{
	struct task* task = kmalloc(sizeof(struct task));
	if (task == NULL) return NULL;
	memset(task, 0, sizeof(struct task));

	task->state = UNREADY;
	create_stack(task);
	task->PID = queue.pid_i++;
	task->cr3 = vmm_read_cr3();
	task->parent = kernel_task;
	if (entry) {
		task->entry = entry;
		task->regs->rip = (uintptr_t)entry;
	}
	task_add(task);

	task->state = READY;

	return task;
}

/// Returns NULL if invalid next task
/// Sets the current task
struct task* scheduler_pick_next()
{
	if (!queue.list || list_empty(queue.list)) return NULL;
	// If we only have 1 task then might as well make sure we continue it
	if (queue.task_count == 1) return queue.current_task;

	struct task* t = queue.current_task;
	for (size_t i = 0; i < queue.task_count; i++) {
		t = list_next_entry(t, list);
		if (t->state == READY) {
			queue.current_task = t;
			return t;
		}
	}
	// No ready task found
	queue.current_task = idle_task;
	return queue.current_task;
}

void scheduler_tick()
{
	struct task* task = queue.current_task;
	for (size_t i = 0; i < queue.task_count; i++) {
		task = list_next_entry(task, list);
		if (task->state == BLOCKED && task->sleep_ticks > 0) {
			task->sleep_ticks--;
			if (task->sleep_ticks == 0) task->state = READY;
		}
	}
}

void yield()
{
	need_reschedule = true;
	__asm__ volatile("int $0x30"); // use an unused vector
}

void yield_blocked()
{
	queue.current_task->state = BLOCKED;
	yield();
}
