#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char** argv, char** envp)
{
	printf("argc: %zu, argv: %p, envp: %p\n", (size_t)argc, argv, envp);
	for (size_t i = 0; i < (size_t)argc; i++) {
		printf("argv[%zu]: %s\n", i, argv[i]);
	}

	while (envp && *envp) {
		printf("envp: %s\n", *envp);
		envp++;
	}

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
		execve("/usr/bin/hello_world.elf", nullptr, nullptr);
		exit(1); // Child process exits
	} else {
		// This code block is executed by the parent process
		printf("Hello from the parent process! My PID is %d, my child's PID is %d.\n",
		       getpid(),
		       pid);
		while (true) {
			int status;
			int fin_pid = waitpid(-1, &status, 0);
			printf("Child process %d finished with status %d.\n",
			       fin_pid,
			       status);
		}
	}
	for (;;)
		;
	return 0;
}
