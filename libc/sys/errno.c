#include "errno.h"
#include "string.h"

static int __errno_value = 0;

int* __errno_location()
{
	static int* errno_ptr = NULL;
	if (!errno_ptr) {
		errno_ptr = &__errno_value;
	}
	return errno_ptr;
}
