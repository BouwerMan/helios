#include "errno.h"

// TODO: Use thread-local storage for errno

static int __errno_value = 0;

int* __errno_location()
{
	return &__errno_value;
}
