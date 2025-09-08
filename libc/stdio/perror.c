#include <helios/errno.h>

#include "errno.h"
#include "stdio.h"

void perror(const char* s)
{
	fprintf(stderr, "%s: %s\n", s, __get_error_string(errno));
}
