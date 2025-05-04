#include <kernel/memory/vmm.h>
#include <kernel/tasks/tasks.h>

extern void __switch_to(struct registers* old, struct registers* new);

void context_switch(struct task* current, struct task* next)
{
	current->cr3 = vmm_read_cr3();
	__switch_to(current->regs, next->regs);
}
