#include "errors.h"


int main(int argc, char **argv)
{
	int seconds;
	char line[128];
	// plus 1 for terminate '\0'
	char message[64 + 1];

	while(1) {
		printf("Alarm>\n");
		// fgets read at most one less than size characters
		// if on error or end of file
		if (fgets(line, sizeof(line), stdin) == NULL) {
			exit(0);
		}

		// line is empty
		if (strlen(line) < 1) {
			continue;
		}

		// parse first token as second number, then next 64 characters as message
		if (sscanf(line, "%d %64[^\n]", &seconds, message) < 2) {
			fprintf(stderr, "bad arguments\n");
		} else {
			sleep(seconds);
			printf("(%d) %s\n", seconds, message);
		}
	}
	return 0;
}
