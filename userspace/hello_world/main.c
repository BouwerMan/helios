#include <arch/syscall.h>
#include <stdio.h>

// You'll need to define this syscall number in your libc headers
#define SYS_TEST_COW 2

// A simple wrapper for the syscall
int test_cow_setup()
{
	return __syscall0(SYS_TEST_COW);
	// long ret;
	// __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_TEST_COW));
	// return (int)ret;
}

int main(void)
{
	printf("Hello from userspace! Preparing to test CoW.\n");

	// These are the addresses the kernel will set up for us.
	char* ptr1 = (char*)0x100000;
	char* ptr2 = (char*)0x200000;

	// Ask the kernel to set up the CoW scenario.
	if (test_cow_setup() != 0) {
		printf("Kernel failed to set up the CoW test.\n");
		return -1;
	}

	// At this point, ptr1 and ptr2 point to the SAME read-only physical page.
	// The initial value should be 'A'.
	printf("Initial data at ptr1: '%c'\n", ptr1[0]);
	printf("Initial data at ptr2: '%c'\n", ptr2[0]);

	printf("Attempting to write to ptr1. This will trigger a CoW page fault...\n");

	// This is the magic moment! This write will fault.
	ptr1[0] = 'B';

	printf("...Write successful! The CoW fault was handled correctly.\n");

	// Now, verify that the copy happened.
	printf("%c\n", ptr1[0]);
	printf("New data at ptr1: '%c'\n", ptr1[0]);
	printf("Data at ptr2 should still be the original: '%c'\n", ptr2[0]);

	if (ptr1[0] == 'B' && ptr2[0] == 'A') {
		printf("SUCCESS: CoW worked as expected!\n");
	} else {
		printf("FAILURE: The data does not match the expected outcome.\n");
	}

	for (;;)
		;
	return 0;
}
