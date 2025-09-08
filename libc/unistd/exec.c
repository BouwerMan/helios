#include "arch/syscall.h"
#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"

// TODO: errno and better error handling

int execve(const char* path, char* const argv[], char* const envp[])
{
	return (int)__syscall3(SYS_EXEC, (long)path, (long)argv, (long)envp);
}

int execv(const char* path, char* const argv[])
{
	return execve(path, argv, environ);
}

static char* find_in_path(const char* cmd);

int execvp(const char* path, char* const argv[])
{

	char* found_path = find_in_path(path);
	if (!found_path) {
		return -1;
	}
	int ret = execve(found_path, argv, environ);
	free(found_path);
	return ret;
}

// NOTE: Caller must free
static char* find_in_path(const char* cmd)
{
	if (strchr(cmd, '/')) {
		// Command contains a slash, treat as a path
		if (access(cmd, X_OK) == 0) {
			return strdup(cmd);
		} else {
			return nullptr;
		}
	}

	char* path_env = getenv("PATH");
	if (!path_env) {
		return nullptr; // No PATH set
	}

	char* path_copy = strdup(path_env);
	char* dir = strtok(path_copy, ":");
	char* result = nullptr;

	while (dir) {
		// +2 for '/' and null terminator
		size_t len = strlen(dir) + strlen(cmd) + 2;
		char* full_path = malloc(len);
		snprintf(full_path, len, "%s/%s", dir, cmd);

		if (access(full_path, X_OK) == 0) {
			result = full_path;
			break;
		}

		free(full_path);
		dir = strtok(nullptr, ":");
	}

	free(path_copy);
	return result;
}
