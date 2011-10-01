#include <sys/types.h>
#include <wait.h>
#include "errors.h"


int main(int argc, char **argv)
{
	int seconds;
	char line[128];
	// plus 1 for terminate '\0'
	char message[64 + 1];

	int status;
	pid_t pid;
	while(1) {
		printf("Alarm>\n");
		// fgets read at most one less than size characters
		// if on error or end of file
		if (fgets(line, sizeof(line), stdin) == NULL) {
			break;
		}

		// line is empty
		if (strlen(line) < 1) {
			continue;
		}

		// parse first token as second number, then next 64 characters as message
		if (sscanf(line, "%d %64[^\n]", &seconds, message) < 2) {
			fprintf(stderr, "bad arguments\n");
		} else {
			pid = fork();
			if (pid == (pid_t)-1) {
				errno_abort("fork");
			} else if (pid == (pid_t)0) {
				// in child process sleep and printf message
				sleep(seconds);
				printf("(%d) %s\n", seconds, message);
			} else {
				// in parent process reap child process
				do {
					pid = waitpid((pid_t)-1, &status, WNOHANG);
					if (pid == (pid_t)-1) {
						errno_abort("waitpid");
					}
				} while (pid != (pid_t)0);
			}
		}
	}
}
