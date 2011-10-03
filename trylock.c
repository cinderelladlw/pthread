#include <pthread.h>
#include "errors.h"

#define SPIN 1000000000

pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;
long counter = 0;
time_t end_time;

void *counter_thread(void *arg)
{
	int status;
	while (time(NULL) < end_time) {
		status = pthread_mutex_lock(&counter_mutex);
		if (status != 0) {
			err_abort(status, "Lock mutex");
		}

		for (int i = 0; i < SPIN; ++i) {
			++counter;
		}
		printf("counter: %lx\n", counter);

		status = pthread_mutex_unlock(&counter_mutex);
		if (status != 0) {
			err_abort(status, "Unlock mutex");
		}

		sleep(1);
	}

	return NULL;
}

void *moniter_thread(void *arg)
{
	int status;
	int misses = 0;
	while (time(NULL) < end_time) {
		sleep(3);

		status = pthread_mutex_trylock(&counter_mutex);

		if (status == 0) {
			printf("counter is %ld\n", counter / SPIN);
			status = pthread_mutex_unlock(&counter_mutex);
			if (status != 0) {
				err_abort(status, "Unlock mutex");
			}
		} else {
			if (status == EBUSY) {
				++misses;
			} else {
				err_abort(status, "Lock mutex");
			}
		}
	}
	printf("Moniter thread missed update %d times\n", misses);
	return NULL;
}


int main()
{
	pthread_t counter_thread_id;
	pthread_t moniter_thread_id;

	int status;

	end_time = time(NULL) + 60;

	status = pthread_create(&counter_thread_id, NULL, counter_thread, NULL);
	if (status != 0) {
		err_abort(status, "Create counter thread");
	}

	status = pthread_create(&moniter_thread_id, NULL, moniter_thread, NULL);
	if (status != 0) {
		err_abort(status, "Create moniter thread");
	}

	status = pthread_join(counter_thread_id, NULL);
	if (status != 0) {
		err_abort(status, "Join counter thread");
	}

	status = pthread_join(moniter_thread_id, NULL);
	if (status != 0) {
		err_abort(status, "Join moniter thread");
	}

	return 0;
}
