#include <stdlib.h>

void exit(int status)
{
	(void)status;
	while (1) {
		__builtin_ia32_pause();
	}

// TODO:
#if 0
    // If we are in kernel mode, halt the system
    if (__is_libk) {
	halt(status);
    }

    // If we are in user mode, terminate the process
    _exit(status);
#endif
}
