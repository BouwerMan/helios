#include "internal/features.h"
#include "internal/stdio.h"

char** __environ = 0; // POSIX standard global
weak_alias(__environ, ___environ);
weak_alias(__environ, _environ);
weak_alias(__environ, environ);

void __init_libc(int argc, char** argv, char** envp)
{
	(void)argc;
	(void)argv;

	__init_streams();
	environ = envp;
}
