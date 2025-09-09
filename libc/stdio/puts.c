#include "internal/features.h"
#include "internal/stdio.h"

int __puts(const char* s)
{
	if (__fputs(s, stdout) < 0 || __fputc('\n', stdout) < 0) {
		return -1;
	}
	return 0;
}
weak_alias(__puts, puts);

int __fputs(const char* __restrict s, FILE* __restrict stream)
{
	for (const char* p = s; *p; p++) {
		if (__fputc(*p, stream) < 0) {
			return -1;
		}
	}
	return 0;
}
weak_alias(__fputs, fputs);
