#include <printf.h>
#include <stdlib.h>

#if defined(__is_libk)
#include <kernel/panic.h>
#endif

__attribute__((__noreturn__)) void abort(void)
{
#if defined(__is_libk)
	panic("Aborting");
#else
	//  TODO: Abnormally terminate the process as if by SIGABRT.
	printf("abort()\n");
#endif
	while (1) {
	}
	__builtin_unreachable();
}
