#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
	pid_t pid;

	// Call fork() to create a new process
	pid = fork();

	// Check the return value of fork()
	if (pid == -1) {
		// Error occurred during fork()
		printf("Fork failed!\n");
		for (;;)
			;
		// perror("fork failed");
		// exit(EXIT_FAILURE);
	} else if (pid == 0) {
		// This code block is executed by the child process
		printf("Hello from the child process! My PID is %d, my parent's PID is %d.\n",
		       getpid(),
		       getppid());
		exec_module("/usr/bin/hello_world.elf");
		exit(1); // Child process exits
	} else {
		// This code block is executed by the parent process
		printf("Hello from the parent process! My PID is %d, my child's PID is %d.\n",
		       getpid(),
		       pid);
		int status;
		waitpid(pid, &status, 0);
		printf("Child process %d finished with status %d.\n",
		       pid,
		       status);
	}
	for (;;)
		;
	return 0;
}
