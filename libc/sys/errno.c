#include "errno.h"
#include "string.h"

int* __errno_location()
{
	static int* errno_ptr = NULL;
	if (!errno_ptr) {
		// errno_ptr = (int*)__syscall_get_errno_ptr();
	}
	return errno_ptr;
}
