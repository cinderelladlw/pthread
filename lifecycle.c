#include <pthread.h>
#include "errors.h"

void *thread_routine(void *arg)
{
	return arg;
}

int main()
{
	pthread_t thread;
	void *thread_result;

	int status;
	status = pthread_create(&thread, NULL, thread_routine, NULL);
	if (status != 0) {
		err_abort(status, "Thread create");
	}

	status = pthread_join(thread, &thread_result);
	if (status != 0) {
		err_abort(status, "Join thread");
	}

	if (thread_result == NULL) {
		return 0;
	} else {
		return 1;
	}
}
