#include <pthread.h>
#include "errors.h"

// Default sleep time
int hibernate = 1;

typedef struct my_struct_tag {
	pthread_mutex_t mutex;		/* protect access to value */
	pthread_cond_t cond;		/* signal change to value */
	int value;			/* predicate: value != 0 */
} my_struct_t;

my_struct_t data = {
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_COND_INITIALIZER,
	0
};

/* Thread start routine. Set predicate for main thread and signal condition variable */
void *wait_thread(void *arg)
{
	int status;
	sleep(hibernate);

	status = pthread_mutex_lock(&data.mutex);
	if (status != 0) {
		err_abort(status, "Lock mutex in wait_thread");
	}

	// set predicate
	data.value = 1;
	status = pthread_cond_signal(&data.cond);
	if (status != 0) {
		err_abort(status, "signal main thread");
	}

	status = pthread_mutex_unlock(&data.mutex);
	if (status != 0) {
		err_abort(status, "Unlock mutex in wait_thread");
	}

	return NULL;
}

int main(int argc, char **argv)
{
	int status;
	pthread_t wait_thread_id;

	if (argc > 1) {
		hibernate = atoi(argv[1]);
	}

	struct timespec timeout;
	timeout.tv_sec = time(NULL) + 2;
	timeout.tv_nsec = 0;

	status = pthread_create(&wait_thread_id, NULL, wait_thread, NULL);
	if (status != 0) {
		err_abort(status, "Create wait thread");
	}

	// get mutex to wait condition
	status = pthread_mutex_lock(&data.mutex);
	if (status != 0) {
		err_abort(status, "Lock mutex");
	}

	// wait condition in a while loop testing predicate
	while (data.value == 0) {
		status = pthread_cond_timedwait(&data.cond, &data.mutex, &timeout);
		if (status == ETIMEDOUT) {
			printf("time out\n");
			printf("predicate %d\n", data.value);
			break;
		} else if (status != 0) {
			err_abort(status, "Timed wait on cond");
		}
	}

	if (data.value != 0) {
		printf("Condition signaled, %d\n", data.value);
	}

	// when return from wait, resume lock for mutex, so unlock it
	status = pthread_mutex_unlock(&data.mutex);
	if (status != 0) {
		err_abort(status, "Unlock mutex");
	}

	return 0;
}
