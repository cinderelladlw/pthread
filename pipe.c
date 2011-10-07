#include <pthread.h>
#include "errors.h"

typedef struct stage_tag {
	struct stage_tag		*link;
	// protect access to stage_tag
	pthread_mutex_t			mutex;
	// the stage is ready to process new data
	pthread_cond_t			ready;
	// the stage has data to process by stage thread
	pthread_cond_t			avail;
	// predicate for cond ready: has_data == 0
	// predicate for cond avail: has_data == 1
	int				has_data;
	// stage thread to process data
	pthread_t			thread;
	// this is our data
	long				data;
} stage_t;


typedef struct pipe_tag {
	// protect access to pipe_tag
	pthread_mutex_t			mutex;
	// point to first stage in the pipeline
	stage_t				*head;
	// point to last stage in the pipeline which does not have a thread with it
	// only contains the result of the pipeline
	stage_t				*tail;
	// number of stages in the pipeline
	int				stages;
	// number of data items
	int				activity;
} pipe_t;


int pipe_send(stage_t *stage, int data)
{
	int status;

	status = pthread_mutex_lock(&stage->mutex);
	if (status != 0) {
		return status;
	}

	// wait on ready when there is data for stage thread to process
	while (stage->has_data) {
		status = pthread_cond_wait(&stage->ready, &stage->mutex);
		if (status != 0) {
			pthread_mutex_unlock(&stage->mutex);
			return status;
		}
	}

	stage->data = data;

	// change predicate for avial
	stage->has_data = 1;
	// signal stage thread to process stage data
	status = pthread_cond_signal(&stage->avail);
	if (status != 0) {
		pthread_mutex_unlock(&stage->mutex);
		return status;
	}

	status = pthread_mutex_unlock(&stage->mutex);
	return status;
}

void *stage_thread(void *arg)
{
	int status;
	stage_t *stage = (stage_t *)arg;
	stage_t *next_stage = stage->link;

	status = pthread_mutex_lock(&stage->mutex);
	if (status != 0) {
		err_abort(status, "Lock mutex in stage thread");
	}

	printf("Enter a number as input or '=' character to get result\n");
	while (1) {
		// wait on avail when there is no data for stage thread to process
		while (!stage->has_data) {
			status = pthread_cond_wait(&stage->avail, &stage->mutex);
			if (status != 0) {
				err_abort(status, "Wait on cond avail in stage thread");
			}
		}

		// process data, plus 1, then pass it to next stage
		pipe_send(next_stage, stage->data + 1);

		// change predicate for ready
		stage->has_data = 0;
		// signal stage it can accept new data
		status = pthread_cond_signal(&stage->ready);
		if (status != 0) {
			err_abort(status, "Signal stage to accept new data");
		}
	}
}

int create_pipe(pipe_t *pipe, int stages)
{
	int status;
	stage_t **link = &pipe->head, *next_stage, *stage;

	pipe->stages = stages;
	pipe->activity = 0;

	status = pthread_mutex_init(&pipe->mutex, NULL);
	if (status != 0) {
		err_abort(status, "Init pipe mutex");
	}

	for (int i = 0; i < stages; ++i) {
		next_stage = malloc(sizeof(stage_t));
		if (next_stage == NULL) {
			errno_abort("Allocate memory for stage");
		}

		status = pthread_mutex_init(&next_stage->mutex, NULL);
		if (status != 0) {
			err_abort(status, "Init stage mutex");
		}

		status = pthread_cond_init(&next_stage->ready, NULL);
		if (status != 0) {
			err_abort(status, "Init stage's ready cond");
		}

		status = pthread_cond_init(&next_stage->avail, NULL);
		if (status != 0) {
			err_abort(status, "Init stage's avail cond");
		}
		next_stage->has_data = 0;
		next_stage->data = 0;

		*link = next_stage;
		link = &next_stage->link;
	}
	// for last stage
	next_stage->link = NULL;
	pipe->tail = next_stage;


	// init stage thread, final stage don't have stage thread, so stage thread don't need to check next_stage is NULL
	for (stage = pipe->head; stage->link != NULL; stage = stage->link) {
		status = pthread_create(&stage->thread, NULL, stage_thread, stage);
		if (status != 0) {
			err_abort(status, "Create stage thread");
		}
	}

	return 0;
}

int pipe_start(pipe_t *pipe, long data)
{
	int status;
	status = pthread_mutex_lock(&pipe->mutex);
	if (status != 0) {
		err_abort(status, "Lock pipe mutex");
	}

	++pipe->activity;

	status = pthread_mutex_unlock(&pipe->mutex);
	if (status != 0) {
		err_abort(status, "Unlock pipe mutex");
	}

	pipe_send(pipe->head, data);

	return 0;
}

// return 0 when pipe is empty
// return 1 otherwise
int pipe_result(pipe_t *pipe, long *result)
{
	int status;
	int empty = 0;
	stage_t *stage;

	status = pthread_mutex_lock(&pipe->mutex);
	if (status != 0) {
		err_abort(status, "Lock pipe mutex");
	}
	if (pipe->activity <= 0) {
		// Can't just return without unlock mutex you have
		// return 0;
		empty = 1;
	} else {
		--pipe->activity;
	}

	status = pthread_mutex_unlock(&pipe->mutex);
	if (status != 0) {
		err_abort(status, "Unlock pipe mutex");
	}

	if (empty) {
		return 0;
	}

	stage = pipe->tail;
	status = pthread_mutex_lock(&stage->mutex);
	if (status != 0) {
		err_abort(status, "Lock tail stage mutex");
	}

	// wait for result
	while (!stage->has_data) {
		status = pthread_cond_wait(&stage->avail, &stage->mutex);
		if (status != 0) {
			err_abort(status, "Wait on tail avail cond");
		}
	}

	*result = stage->data;

	// change predicate for stage ready cond
	stage->has_data = 0;
	// signal stage ready cond
	status = pthread_cond_signal(&stage->ready);

	status = pthread_mutex_unlock(&stage->mutex);
	if (status != 0) {
		err_abort(status, "Unlock tail stage mutex");
	}

	return 1;
}

int main(int argc, char **argv)
{
	char line[128];
	pipe_t pipe;
	long result, data;

	create_pipe(&pipe, 5);

	while (1) {
		printf("Data>\n");
		if (fgets(line, sizeof(line), stdin) == NULL) {
			exit(0);
		}

		// line will contains the '\n'
		if (strlen(line) <= 1) {
			continue;
		}

		if (strlen(line) == 2 && line[0] == '=') {
			if (pipe_result(&pipe, &result)) {
				printf("Result is %ld\n", result);
			} else {
				printf("The pipe is empty\n");
			}
		} else {
			if (sscanf(line, "%ld", &data) != 1) {
				fprintf(stderr, "Bad input data\n");
			} else {
				pipe_start(&pipe, data);
			}
		}
	}
	return 0;
}
