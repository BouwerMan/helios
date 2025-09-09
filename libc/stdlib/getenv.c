#include "internal/features.h"
#include "internal/unistd.h"
#include "string.h"

char* __getenv(const char* name)
{
	if (!name || !__environ) {
		return nullptr;
	}

	size_t name_len = strlen(name);

	for (int i = 0; __environ[i] != nullptr; i++) {
		if (strncmp(__environ[i], name, name_len) == 0 &&
		    __environ[i][name_len] == '=') {
			return &__environ[i][name_len + 1];
		}
	}

	return nullptr;
}
weak_alias(__getenv, getenv);
