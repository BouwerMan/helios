#include <cpuid.h>

#include "arch/tsc.h"
#include "kernel/bitops.h"
#include "kernel/panic.h"
#include "lib/log.h"

static constexpr u32 HV_X64_MSR_TSC_FREQUENCY = 0x40000022;

u64 __tsc_hz = 0;

// TODO: Move these
bool cpu_has_msr()
{
	static constexpr u32 CPUID_FLAG_MSR = 1 << 5;
	unsigned int eax, ebx, ecx, edx;
	__get_cpuid(0x1, &eax, &ebx, &ecx, &edx);
	return CHECK_BIT(edx, CPUID_FLAG_MSR);
}

void cpu_rdmsr(u32 msr, u32* lo, u32* hi)
{
	__asm__ __volatile__("rdmsr" : "=a"(*lo), "=d"(*hi) : "c"(msr));
}

bool tsc_is_invariant(void)
{
	unsigned int edx, unused;
	__get_cpuid(0x80000007, &unused, &unused, &unused, &edx);
	return CHECK_BIT(edx, 8);
}

bool tsc_try_cpuid15(u64* out_hz)
{
	unsigned int eax, ebx, ecx, edx;
	int res = __get_cpuid(0x15, &eax, &ebx, &ecx, &edx);
	log_debug("CPUID(0x15): eax=%u, ebx=%u, ecx=%u, edx=%u",
		  eax,
		  ebx,
		  ecx,
		  edx);
	if (eax == 0 || ebx == 0 || ecx == 0)
		return false; // ratio/crystal missing
	*out_hz = (u64)((unsigned __int128)ecx * ebx / eax); // Hz
	return true;
	return res;
}

bool sys_hypervisor()
{
	unsigned int eax, ebx, ecx, edx;
	__get_cpuid(0x1, &eax, &ebx, &ecx, &edx);
	return CHECK_BIT(ecx, 31);
}

bool tsc_try_hv(u64* out_hz)
{
	if (!sys_hypervisor()) {
		log_debug("Not running under a hypervisor");
		return false;
	}

	u32 lo, hi;

	cpu_rdmsr(HV_X64_MSR_TSC_FREQUENCY, (u32*)&lo, (u32*)&hi);

	*out_hz = ((u64)hi << 32) | lo;

	if (!*out_hz) {
		unsigned int eax, ebx, ecx, edx;
		__get_cpuid(0x40000010, &eax, &ebx, &ecx, &edx);
		if (eax) {
			*out_hz = (u64)eax * 1000;
			return true;
		}
		return false;
	}

	log_debug("Hypervisor TSC frequency: %lu Hz", *out_hz);

	return true;
}

void tsc_init(void)
{
	if (!tsc_is_invariant()) {
		if (tsc_try_hv(&__tsc_hz)) {
			goto out;
		}
		log_error("TSC is not invariant");
		// TODO: Determine TSC frequency with HPET or PMTMR
		log_warn(
			"Falling back to hardcoded value (we are probably in GDB)");
		__tsc_hz = 3609600000;
		return;
	}

	log_debug("TSC is invariant");

	if (tsc_try_cpuid15(&__tsc_hz)) {
		goto out;
	}
	if (tsc_try_hv(&__tsc_hz)) {
		goto out;
	}

	panic("Failed to determine TSC frequency");

out:
	log_debug("TSC frequency: %lu Hz", __tsc_hz);
}
