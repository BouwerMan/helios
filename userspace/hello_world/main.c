#include <stdio.h>

int main(int argv, char** argc)
{
	volatile int num = 12;
	printf("Hello World!\n");
	printf("Here is a number: %d\n", num);
	return 0;
}
