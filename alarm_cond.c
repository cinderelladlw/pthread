#include <pthread.h>
#include "errors.h"

typedef struct alarm_tag {
	struct alarm_tag	*link;			/* point to next alarm */
	time_t			time;			/* expiration time from Epoch */
	int			seconds;		/* relative time */
	char			message[64 + 1];	/* alarm message */
}alarm_t;

/* protect access to alarm list */
pthread_mutex_t alarm_mutex = PTHREAD_MUTEX_INITIALIZER;
/* signal change to alarm list */
pthread_cond_t alarm_cond = PTHREAD_COND_INITIALIZER;


alarm_t *alarm_list = NULL;

/* optimization for signal, only list is empty or insert a new earlier alarm */
time_t current_time = 0;

/* insert alarm in list in order, caller MUST have alarm_metux locked */
void insert_alarm(alarm_t *alarm)
{
	int status;
	alarm_t **last = &alarm_list;
	alarm_t *next = alarm_list;

	//DPRINTF(("insert alarm %ld(%d) %s\n", alarm->time, alarm->seconds, alarm->message));

	while (next != NULL) {
		if (next->time > alarm->time) {
			*last = alarm;
			alarm->link = next;
			break;
		}
		last = &next->link;
		next = next->link;
	}

	/* empty list or insert at list tail */
	if (next == NULL) {
		*last = alarm;
		alarm->link = NULL;
	}
#ifdef DEBUG
	printf("[list\n");
	for (next = alarm_list; next != NULL; next = next->link) {
		printf("%ld(%d) %s\n", next->time, next->seconds, next->message);
	}
	printf("]\n");
#endif
	/* emtpy list or insert a new earlier alarm */
	if (current_time == 0 || current_time > alarm->time) {
		//DPRINTF(("signal alarm_thread, current_time %ld, alarm->time %ld\n", current_time, alarm->time));
		current_time = alarm->time;
		status = pthread_cond_signal(&alarm_cond);
		if (status != 0) {
			err_abort(status, "Signal alarm cond");
		}
	}
}

/* alarm thread start function */
void *alarm_thread(void *arg)
{
	int status;
	int expired;
	time_t now;
	alarm_t *alarm;

	while(1) {
		status = pthread_mutex_lock(&alarm_mutex);
		if (status != 0) {
			err_abort(status, "Lock mutex");
		}

		current_time = 0;
		while (alarm_list == NULL) {
			//DPRINTF(("wait on empty list\n"));
			status = pthread_cond_wait(&alarm_cond, &alarm_mutex);
			if (status != 0) {
				err_abort(status, "Wait on empty list");
			}
		}

		expired = 0;
		alarm = alarm_list;
		alarm_list = alarm->link;
		now = time(NULL);

		if (alarm->time > now) {
			struct timespec timeout;
			timeout.tv_sec = alarm->time;
			timeout.tv_nsec = 0;
			current_time = alarm->time;
			while (current_time == alarm->time) {
				//DPRINTF(("wait on alarm %ld(%d) %s\n", alarm->time, alarm->seconds, alarm->message));
				status = pthread_cond_timedwait(&alarm_cond, &alarm_mutex, &timeout);
				if (status != 0) {
					if (status == ETIMEDOUT) {
						//DPRINTF(("alarm expired by time out\n"));
						expired = 1;
						break;
					} else {
						err_abort(status, "Timed wait on alarm");
					}
				}
			}

			// signal by new earlier alarm, so reinsert current unexpired alarm
			if (!expired) {
				//DPRINTF(("reinsert alarm %ld(%d) %s\n", alarm->time, alarm->seconds, alarm->message));
				insert_alarm(alarm);
			}
		} else {
			//DPRINTF(("alarm already expired\n"));
			expired = 1;
		}

		status = pthread_mutex_unlock(&alarm_mutex);
		if (status != 0) {
			err_abort(status, "Unlock mutex");
		}

		if (expired) {
			printf("(%d) %s\n", alarm->seconds, alarm->message);
			free(alarm);
		}
	}
	return NULL;
}

int main()
{
	int status;
	pthread_t alarm_thread_id;
	char line[128];
	alarm_t *alarm;

	status = pthread_create(&alarm_thread_id, NULL, alarm_thread, NULL);
	if (status != 0) {
		err_abort(status, "Create alarm thread");
	}

	while(1) {
		printf("Alarm>\n");

		if (fgets(line, sizeof(line), stdin) == NULL) {
			exit(0);
		}

		if (strlen(line) < 1) {
			continue;
		}

		alarm = malloc(sizeof(alarm_t));
		if (alarm == NULL) {
			errno_abort("Allocate memory for alarm");
		}

		if (sscanf(line, "%d %64[^\n]", &alarm->seconds, alarm->message) < 2) {
			fprintf(stderr, "Bad command");
			free(alarm);
		} else {
			status = pthread_mutex_lock(&alarm_mutex);
			if (status != 0) {
				err_abort(status, "Lock mutex");
			}

			alarm->time = time(NULL) + alarm->seconds;
			alarm->link = NULL;

			insert_alarm(alarm);

			status = pthread_mutex_unlock(&alarm_mutex);
			if (status != 0) {
				err_abort(status, "Unlock mutex");
			}
		}
	}
	return 0;
}
