#include "kernel/time.h"
#include "arch/tsc.h"
#include "kernel/panic.h"

static struct clock tsc = { 0 };

void clock_init(u64 (*read)(void), u64 counter_hz)
{
	if (counter_hz == 0 || !read) {
		panic("Invalid arguments to clock_init");
	}

	tsc.base_counter = read();
	tsc.read = read;

	tsc.counter_hz = counter_hz;
	tsc.count2ns_shift = 32;
	tsc.count2ns_mul =
		(u64)(((unsigned __int128)1000000000ULL << tsc.count2ns_shift) /
		      tsc.counter_hz);
	tsc.base_ns = 0;
}

[[gnu::hot]]
u64 clock_now_ns(void)
{
	u64 t = tsc.read();
	u64 dt = t - tsc.base_counter;
	unsigned __int128 acc = (unsigned __int128)dt * tsc.count2ns_mul;
	u64 ns = tsc.base_ns + (u64)(acc >> tsc.count2ns_shift);
	return ns;
}

u64 clock_now_ms(void)
{
	return clock_now_ns() / 1000000ULL;
}
