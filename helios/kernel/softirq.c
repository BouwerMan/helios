#include "kernel/softirq.h"
#include "kernel/assert.h"

u64 g_pending_bits = 0;

void try_softirq(void)
{
	kassert(g_pending_bits != 0, "No pending softirqs");
}
