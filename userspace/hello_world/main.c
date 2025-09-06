#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

int main(void)
{
	printf("Hello, World!\n");

	int c;
	c = getchar();

	printf("You entered: %c\n", c);

	return 0;
}
