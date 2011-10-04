#include <pthread.h>
#include "errors.h"

pthread_mutex_t mutexs[3] = {
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_MUTEX_INITIALIZER
};

#define ITERATION 10

// if backoff is nonzero, then use backoff algorithm
int backoff = 1;
// if yield_flag == 0, do nothing
// if yield_flag > 0, then call sched_yield
// if yield_flag < 0, then sleep to make lock collision more obvious
int yield_flag = 0;


// lock mutex in order
void *lock_forward(void *arg)
{
	int status;
	int backoff_count;
	for (int iter = 0; iter < ITERATION; ++iter) {
		backoff_count = 0;
		for (int i = 0; i < 3; ++i) {
			if (i == 0) {
				status = pthread_mutex_lock(&mutexs[i]);
				if (status != 0) {
					err_abort(status, "First mutex");
				}
				DPRINTF(("lock forward get %dth mutex\n", i));
			} else {
				if (backoff) {
					status = pthread_mutex_trylock(&mutexs[i]);
				} else {
					status = pthread_mutex_lock(&mutexs[i]);
				}

				// lock collision
				if (status == EBUSY) {
					++backoff_count;
					DPRINTF(("lock forward collide at %d\n", i));
					// backoff
					for (; i >= 0; --i) {
						status = pthread_mutex_unlock(&mutexs[i]);
						if (status != 0) {
							err_abort(status, "Unlock mutex");
						}
					}
				} else {
					if (status != 0) {
						err_abort(status, "Lock mutex");
					}
					DPRINTF(("lock forward get %dth mutex\n", i));
				}
			}

			if (yield_flag != 0) {
				if (yield_flag > 0) {
					status = sched_yield();
					if (status != 0) {
						err_abort(status, "Yield");
					}
				} else {
					sleep(1);
				}
			}
		}
		printf("lock forward get all mutexs, backoff count: %d\n", backoff_count);
		for (int i = 0; i < 3; ++i) {
			status = pthread_mutex_unlock(&mutexs[i]);
			if (status != 0) {
				err_abort(status, "Unlock mutex");
			}
		}
		status = sched_yield();
		if (status != 0) {
			err_abort(status, "Yield");
		}

	}

	return NULL;
}

void *lock_backward(void *arg)
{
	int status;
	int backoff_count;
	for (int iter = 0; iter < ITERATION; ++iter) {
		backoff_count = 0;
		for (int i = 2; i >= 0; --i) {
			if (i == 2) {
				status = pthread_mutex_lock(&mutexs[i]);
				if (status != 0) {
					err_abort(status, "First mutex");
				}
				DPRINTF(("lock backward get %dth mutex\n", i));
			} else {
				if (backoff) {
					status = pthread_mutex_trylock(&mutexs[i]);
				} else {
					status = pthread_mutex_lock(&mutexs[i]);
				}

				if (status == EBUSY) {
					++backoff_count;
					DPRINTF(("lock backward collide at %d\n", i));
					// backoff
					for (; i < 3; ++i) {
						status = pthread_mutex_unlock(&mutexs[i]);
						if (status != 0) {
							err_abort(status, "Unlock mutex");
						}
					}
				} else {
					if (status != 0) {
						err_abort(status, "Lock mutex");
					}
					DPRINTF(("lock backward get %dth mutex\n", i));
				}
			}

			if (yield_flag != 0) {
				if (yield_flag > 0) {
					status = sched_yield();
					if (status != 0) {
						err_abort(status, "Yield");
					}
				} else {
					sleep(1);
				}
			}
		}
		printf("lock backward get all mutexs, backoff count: %d\n", backoff_count);
		for (int i = 2; i >= 0; i--) {
			status = pthread_mutex_unlock(&mutexs[i]);
			if (status != 0) {
				err_abort(status, "Unlock mutex");
			}
		}
	}
	return NULL;
}

int main(int argc, char **argv)
{
	int status;
	pthread_t forward, backward;

	if (argc > 1) {
		backoff = atoi(argv[1]);
	}

	if (argc > 2) {
		yield_flag = atoi(argv[2]);
	}

	status = pthread_create(&forward, NULL, lock_forward, NULL);
	if (status != 0) {
		err_abort(status, "Create lock_forward");
	}

	status = pthread_create(&backward, NULL, lock_backward, NULL);
	if (status != 0) {
		err_abort(status, "Create lock_backward");
	}

	pthread_exit(NULL);
}
