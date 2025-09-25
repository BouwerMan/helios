#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int hsh_cd(char** args);
int hsh_pwd(char** args);
int hsh_ls(char** args);
int hsh_clear(char** args);
int hsh_help(char** args);
int hsh_exit(char** args);
int hsh_shutdown(char** args);

struct terminal {
	size_t rows;
	size_t cols;

	int write_x;
	int write_y;
};

/*
  List of builtin commands, followed by their corresponding functions.
 */
const char* builtin_str[] = {
	"cd", "pwd", "ls", "clear", "help", "exit", "shutdown",
};

int (*builtin_func[])(char**) = { &hsh_cd,	&hsh_pwd,  &hsh_ls,
				  &hsh_clear,	&hsh_help, &hsh_exit,
				  &hsh_shutdown };

int lsh_num_builtins()
{
	return sizeof(builtin_str) / sizeof(char*);
}

int launch(const char* path, char** args)
{
	pid_t pid, wpid;
	int status = 1;

	pid = fork();
	if (pid == 0) {
		//Child process
		execvp(path, args);
		exit(-1);
	} else if (pid < 0) {
		// Error
		fprintf(stderr, "hsh: fork failed\n");
	} else {
		// Parent process
		do {
			wpid = waitpid(pid, &status, 0);
		} while (false); // TODO: IMPLMENT WIFEXITED AND WIFSIGNALED
	}

	return status;
}

/*
  Builtin function implementations.
*/
int hsh_cd(char** args)
{
	if (args[1] == NULL) {
		fprintf(stderr, "hsh: expected argument to \"cd\"\n");
	} else {
		if (chdir(args[1]) != 0) {
			perror("hsh: cd");
		}
	}
	return 0;
}

int hsh_pwd(char** args)
{
	(void)args;
	char buf[256];
	printf("%s\n", getcwd(buf, 256));
	return 0;
}

char get_type_indicator(unsigned char d_type)
{
	switch (d_type) {
	case DT_DIR:  return '/';
	case DT_LNK:  return '@';
	case DT_FIFO: return '|';
	case DT_SOCK: return '=';
	case DT_CHR:
	case DT_BLK:  return '#'; // Or separate chars
	default:      return '\0';	  // Regular files get no indicator
	}
}

int hsh_ls(char** args)
{
	bool show_hidden = false;
	bool show_indicators = false;
	const char* path = ".";

	char* arg = *(++args); // skip "ls"
	while (arg) {
		if (strcmp(arg, "-a") == 0) {
			show_hidden = true;
		} else if (strcmp(arg, "-F") == 0) {
			show_indicators = true;
		} else if (arg[0] != '-') {
			path = arg;
		}
		arg = *(++args);
	}

	DIR* dir = opendir(path);
	if (!dir) {
		perror("hsh: ls");
		// fprintf(stderr, "hsh: ls: cannot access '%s'\n", path);
		return 1;
	}
	struct dirent* entry;
	errno = 0; // Clear errno before readdir()

	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.' && !show_hidden) {
			errno = 0; // Clear errno before next readdir()
			continue;
		}

		if (show_indicators) {
			char indicator = get_type_indicator(entry->d_type);
			if (indicator) {
				printf("%s%c\n", entry->d_name, indicator);
			} else {
				printf("%s\n", entry->d_name);
			}
		} else {
			printf("%s\n", entry->d_name);
		}

		errno = 0; // Clear errno before next readdir()
	}

	// Now check what NULL means
	if (errno == ENOTDIR) {
		// We just print out the single file
		printf("%s\n", path);
	} else if (errno != 0) {
		// Error occurred during readdir()
		perror("readdir failed");
		// fprintf(stderr,
		// 	"hsh: ls: error reading directory '%s'\n",
		// 	path);
		return errno;
	}

	closedir(dir);
	return 0;
}

int hsh_clear(char** args)
{
	(void)args;
	printf("\x1b[2J\x1b[H");
	return 0;
}

int hsh_help(char** args)
{
	(void)args;
	printf("Help yourself fucker\n");
	printf("Here are the builtin commands:\n");
	for (int i = 0; i < lsh_num_builtins(); i++) {
		printf("  %s\n", builtin_str[i]);
	}
	return 0;
}

int hsh_exit(char** args)
{
	(void)args;
	return -1;
}

int hsh_shutdown(char** args)
{
	(void)args;
#ifdef shutdown
	shutdown();
#endif
	return 0;
}

int execute(char** args)
{
	if (args[0] == nullptr) {
		return 1; // Empty command
	}

	for (int i = 0; i < lsh_num_builtins(); i++) {
		if (strcmp(args[0], builtin_str[i]) == 0) {
			return (*builtin_func[i])(args);
		}
	}

	int ret = launch(args[0], args);
	if (ret == -1) {
		fprintf(stderr, "hsh: command not found: %s\n", args[0]);
		return 1;
	}
	return ret;
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

void handle_escape()
{
	char buffer[32] = { 0 };
	size_t i = 0;
	while (true) {
		int c = getchar();
		if (c == EOF || c == '\n') {
			break;
		}
		buffer[i++] = (char)(c);
		if (isalpha(c)) {
			break;
		}
	}
	printf("\x1b%s", buffer);
	fflush(stdout);
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
		if (isprint(c)) {
			buffer[position] = (char)c;
			position++;
			putchar(c);
		} else if (iscntrl(c)) {
			switch (c) {
			case 3: // Ctrl-C
				putchar('^');
				putchar('C');
				putchar('\n');
				buffer[0] = '\0';
				return buffer;
			case '\n':
				putchar(c);
				buffer[position] = '\0';
				return buffer;
			case '\b':
				if (position > 0) {
					putchar(c); // Echo the character
					buffer[--position] = '\0';
				}
				break;
			case '\x1b': handle_escape(); break;
			}
		}

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
	int status = 0;

	do {
		printf("\x1b[1;%dm"
		       "%03d > "
		       "\x1b[0m",
		       status > 0 ? 31 : 32,
		       status);
		fflush(stdout);
		line = read_line();
		args = split_line(line);
		status = execute(args);

		free(line);
		free(args);
	} while (status >= 0);
}

int main(void)
{
	hsh_clear(nullptr);

	printf("Welcome to hsh! Type 'help' for a list of commands.\n");
	char* cols_str = getenv("COLUMNS");
	char* rows_str = getenv("ROWS");
	int cols = atoi(cols_str);
	int rows = atoi(rows_str);

	printf("Terminal size: %sx%s\n",
	       cols_str ? cols_str : "?",
	       rows_str ? rows_str : "?");

	// hsh_shutdown(nullptr);

	hsh_loop();

	return 0;
}
