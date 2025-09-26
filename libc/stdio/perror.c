#include <helios/errno.h>

#include "errno.h"
#include "internal/features.h"
#include "internal/stdio.h"

void __perror(const char* s)
{
	__fprintf(stderr, "%s: %s\n", s, __get_error_string(errno));
}
weak_alias(__perror, perror);
