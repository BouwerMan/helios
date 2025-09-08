#include "ctype.h"
#include "stdlib.h"

int atoi(const char* nptr)
{
	if (!nptr) return 0;

	while (*nptr && isspace(*nptr)) {
		nptr++;
	}

	int sign = 1;
	if (*nptr == '-' || *nptr == '+') {
		sign = (*nptr == '-') ? -1 : 1;
		nptr++;
	}

	int result = 0;
	while (*nptr >= '0' && *nptr <= '9') {
		result = result * 10 + (*nptr - '0');
		nptr++;
	}

	return result * sign;
}
