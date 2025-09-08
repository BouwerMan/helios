#include <helios/errno.h>

#include "string.h"

const char* strerror(int errnum)
{
	return __get_error_string(errnum);
}
