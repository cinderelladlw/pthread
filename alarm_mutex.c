#include <pthread.h>
#include "errors.h"

typedef struct alarm_type {
	// point to next alarm request
	struct alarm_type		*link;
	// expiration time for this alarm
	time_t				time;
	// requested seconds
	int				seconds;
	// plus 1 for terminate '\0'
	char				message[64 + 1];
} alarm_t;

pthread_mutex_t alarm_mutex = PTHREAD_MUTEX_INITIALIZER;
alarm_t *alarm_list = NULL;


void *alarm_thread(void *arg)
{
	int sleep_time;
	alarm_t *alarm;
	time_t now;
	int status;

	// alarm thread will evaporate after process exit
	while (1) {
		status = pthread_mutex_lock(&alarm_mutex);
		if (status != 0) {
			err_abort(status, "Lock alarm mutex");
		}
		alarm = alarm_list;

		if (alarm == NULL) {
			sleep_time = 1;
		} else {
			alarm_list = alarm->link;
			now = time(NULL);
			if (now == -1) {
				errno_abort("Get time");
			}

			if (alarm->time <= now) {
				sleep_time = 0;
			} else {
				sleep_time = alarm->time - now;
			}
#ifdef DEBUG
			printf("%ld(%d) %s\n", alarm->time, sleep_time, alarm->message);
#endif
		}

		// release mutex before sleep for main thread to add new alarm request to alarm_list
		status = pthread_mutex_unlock(&alarm_mutex);
		if (status != 0) {
			err_abort(status, "Unlock alarm mutex");
		}

		if (sleep_time > 0) {
			sleep(sleep_time);
		} else {
			sched_yield();
		}

		if (alarm != NULL) {
			printf("(%d)->%s\n", alarm->seconds, alarm->message);
			free(alarm);
		}
	}
}

int main()
{
	int status;
	char line[128];
	alarm_t *alarm;
	pthread_t thread;

	status = pthread_create(&thread, NULL, alarm_thread, NULL);
	if (status != 0) {
		err_abort(status, "Create alarm thread");
	}

	while (1) {
		printf("Alarm>\n");
		if (fgets(line, sizeof(line), stdin) == NULL) {
			exit(0);
		}

		if (strlen(line) < 1) {
			continue;
		}

		alarm = malloc(sizeof(alarm_t));
		if (alarm == NULL) {
			errno_abort("Allocate alarm_t");
		}

		if (sscanf(line, "%d %64[^\n]", &alarm->seconds, alarm->message) < 2) {
			fprintf(stderr, "Bad command");
			free(alarm);
		} else {
			status = pthread_mutex_lock(&alarm_mutex);
			if (status != 0) {
				err_abort(status, "Lock alarm mutex");
			}

			alarm->time = time(NULL) + alarm->seconds;

			// traversal single alarm list which is sorted by expiration time
			// last point to head or previous alarm's link member
			// next point to alarm structure to check
			alarm_t **last = &alarm_list;
			alarm_t *next = alarm_list;
			while (next != NULL) {
				if (next->time > alarm->time) {
					alarm->link = next;
					*last = alarm;
					break;
				}
				last = &next->link;
				next = next->link;
			}

			// if list is empty or insert at end of list
			if (next == NULL) {
				*last = alarm;
				alarm->link = NULL;
			}
#ifdef DEBUG
			printf("[list:\n");
			for (next = alarm_list; next != NULL; next = next->link) {
				printf("%ld(%ld)\"%s\"\n", alarm->time, alarm->time - time(NULL), alarm->message);
			}
			printf("]\n");
#endif
			status = pthread_mutex_unlock(&alarm_mutex);
			if (status != 0) {
				err_abort(status, "Unlock alarm mutex");
			}
		}
	}
	return 0;
}
