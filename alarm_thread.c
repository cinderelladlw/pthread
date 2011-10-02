#include <pthread.h>
#include "errors.h"

typedef struct alarm_tag {
	int seconds;
	// plus 1 for terminate '\0'
	char message[64 + 1];
} alarm_t;


void *alarm_thread(void *arg)
{
	alarm_t *alarm = (alarm_t *)arg;

	int status;
	status = pthread_detach(pthread_self());
	if (status != 0) {
		err_abort(status, "Detach thread");
	}
	sleep(alarm->seconds);
	printf("(%d)->%s\n", alarm->seconds, alarm->message);
	free(alarm);
	return NULL;
}

int main()
{
	int status;
	pthread_t pthread;
	char line[128];

	while (1) {
		printf("Alarm>\n");
		if (fgets(line, sizeof(line), stdin) == NULL) {
			exit(0);
		}

		if (strlen(line) < 1) {
			continue;
		}
		
		alarm_t *alarmptr = (alarm_t *)malloc(sizeof(alarm_t));
		if (alarmptr == NULL) {
			errno_abort("Allocation alarm");
		}

		if (sscanf(line, "%d %64[^\n]", &alarmptr->seconds, alarmptr->message) < 2) {
			fprintf(stderr, "Bad command\n");
			free(alarmptr);
		} else {
			status = pthread_create(&pthread, NULL, alarm_thread, alarmptr);
			if (status != 0) {
				err_abort(status, "Create alarm thread");
			}
		}
	}
	return 0;
}
