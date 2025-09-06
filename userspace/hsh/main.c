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
	} else if (pid < 0) {
		// Error
		fprintf(stderr, "hsh: fork failed\n");
	} else {
		// Parent process
		do {
			wpid = waitpid(pid, &status, 0);
		} while (false); // TODO: IMPLMENT WIFEXITED AND WIFSIGNALED
	}

	return 1;
}

/*
  Builtin function implementations.
*/
int hsh_cd(char** args)
{
	if (args[1] == NULL) {
		fprintf(stderr, "lsh: expected argument to \"cd\"\n");
	} else {
		if (chdir(args[1]) != 0) {
			// perror
		}
	}
	return 1;
}

int hsh_help(char** args)
{
	printf("Help yourself fucker\n");
	return 1;
}

int hsh_exit(char** args)
{
	return 0;
}

/*
  List of builtin commands, followed by their corresponding functions.
 */
const char* builtin_str[] = {
	"cd",
	"help",
	"exit",
};

int (*builtin_func[])(char**) = { &hsh_cd, &hsh_help, &hsh_exit };

int lsh_num_builtins()
{
	return sizeof(builtin_str) / sizeof(char*);
}

int execute(char** args)
{
	printf("DEBUG: builtin_str[0] = %p\n", builtin_str[0]);
	printf("DEBUG: lsh_num_builtins() = %d\n", lsh_num_builtins());
	if (args[0] == nullptr) {
		return 1; // Empty command
	}

	for (int i = 0; i < lsh_num_builtins(); i++) {
		if (strcmp(args[0], builtin_str[i]) == 0) {
			return (*builtin_func[i])(args);
		}
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
	char buf[256];
	printf("CWD: %s\n", getcwd(buf, 256));

	chdir("/usr/bin");
	memset(buf, 0, 256);
	printf("CWD: %s\n", getcwd(buf, 256));

	hsh_loop();

	return 0;
}
