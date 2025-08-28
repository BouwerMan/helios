#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

int main(void)
{
	void* res =
		mmap(NULL, 4096, PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS, -1, 0);

	printf("mmap result: %p\n", res);

	char* buffer = res;

	strcpy(buffer, "Hello, World!\n");

	printf("mmap buffer: %s\n", buffer);

	// GDB BREAKPOINT
	char* buffer2 = malloc(strlen(buffer) + 1);
	printf("Liballoc returned: %p\n", (void*)buffer2);
	strcpy(buffer2, buffer);

	printf("Liballoc: %s\n", buffer2);

	for (;;)
		;
	return 0;
}
