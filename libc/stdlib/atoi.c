#include "internal/ctype.h"
#include "internal/features.h"
#include "internal/stdlib.h"

int __atoi(const char* nptr)
{
	if (!nptr) return 0;

	while (*nptr && __isspace(*nptr)) {
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
weak_alias(__atoi, atoi);
