#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int launch(char** args)
{
	pid_t pid, wpid;
	int status;

	pid = fork();
	if (pid == 0) {
		//Child process
		execve(args[0], args, nullptr);
		exit(1);
	} else {
		// Parent process
		do {
			wpid = waitpid(pid, &status, 0);
		} while (false); // TODO: IMPLMENT WIFEXITED AND WIFSIGNALED
	}

	return 1;
}

int execute(char** args)
{
	// TODO: Built-in commands (kernel doesn't support anything lol)
	if (args[0] == nullptr) {
		return 1; // Empty command
	}

	return launch(args);
}

#define HSH_TOK_BUFSIZE 64
#define HSH_TOK_DELIM	" \t\r\n\a"
char** split_line(char* line)
{
	size_t bufsize = HSH_TOK_BUFSIZE;
	size_t position = 0;
	char** tokens = malloc(bufsize * sizeof(char*));
	char* token;

	if (!tokens) {
		fprintf(stderr, "hsh: allocation error\n");
		exit(1);
	}

	token = strtok(line, HSH_TOK_DELIM);
	while (token != NULL) {
		tokens[position] = token;
		position++;

		if (position >= bufsize) {
			bufsize += HSH_TOK_BUFSIZE;
			tokens = realloc(tokens, bufsize * sizeof(char*));
			if (!tokens) {
				fprintf(stderr, "hsh: allocation error\n");
				exit(1);
			}
		}

		token = strtok(NULL, HSH_TOK_DELIM);
	}

	tokens[position] = NULL;
	return tokens;
}

#define LSH_RL_BUFSIZE 1024
char* read_line()
{
	size_t bufsize = LSH_RL_BUFSIZE;
	size_t position = 0;
	char* buffer = malloc(bufsize * sizeof(char));
	int c;

	if (!buffer) {
		fprintf(stderr, "hsh: allocation error\n");
		exit(1);
	}

	while (1) {
		c = getchar();
		putchar(c); // Echo the character

		// If we hit EOF, replace it with a null character and return.
		if (c == EOF || c == '\n') {
			buffer[position] = '\0';
			return buffer;
		} else {
			buffer[position] = (char)c;
		}

		position++;

		if (position >= bufsize) {
			bufsize += LSH_RL_BUFSIZE;
			buffer = realloc(buffer, bufsize);
			if (!buffer) {
				fprintf(stderr, "hsh: allocation error\n");
				exit(1);
			}
		}
	}
}

void hsh_loop()
{
	char* line;
	char** args;
	int status;

	do {
		printf("> ");
		fflush(stdout);
		line = read_line();
		args = split_line(line);
		status = execute(args);

		free(line);
		free(args);
	} while (status);
}

int main(int argc, char** argv, char** envp)
{
	printf("Hello from hsh!\n");

	hsh_loop();

	return 0;
}
