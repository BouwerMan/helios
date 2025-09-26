#include "arch/syscall.h"
#include "internal/features.h"
#include "internal/stdlib.h"
#include "internal/unistd.h"
#include "stdio.h"
#include "string.h"

// TODO: errno and better error handling

int __execve(const char* path, char* const argv[], char* const envp[])
{
	return (int)__syscall3(SYS_EXEC, (long)path, (long)argv, (long)envp);
}
weak_alias(__execve, execve);

int __execv(const char* path, char* const argv[])
{
	return __execve(path, argv, environ);
}
weak_alias(__execv, execv);

static char* find_in_path(const char* cmd);

int __execvp(const char* path, char* const argv[])
{

	char* found_path = find_in_path(path);
	if (!found_path) {
		return -1;
	}
	int ret = __execve(found_path, argv, environ);
	__free(found_path);
	return ret;
}
weak_alias(__execvp, execvp);

// NOTE: Caller must free
static char* find_in_path(const char* cmd)
{
	if (strchr(cmd, '/')) {
		// Command contains a slash, treat as a path
		if (__access(cmd, X_OK) == 0) {
			return strdup(cmd);
		} else {
			return nullptr;
		}
	}

	char* path_env = __getenv("PATH");
	if (!path_env) {
		return nullptr; // No PATH set
	}

	char* path_copy = strdup(path_env);
	char* dir = strtok(path_copy, ":");
	char* result = nullptr;

	while (dir) {
		// +2 for '/' and null terminator
		size_t len = strlen(dir) + strlen(cmd) + 2;
		char* full_path = __malloc(len);
		snprintf(full_path, len, "%s/%s", dir, cmd);

		if (__access(full_path, X_OK) == 0) {
			result = full_path;
			break;
		}

		__free(full_path);
		dir = strtok(nullptr, ":");
	}

	__free(path_copy);
	return result;
}
