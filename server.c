#include <pthread.h>
#include "errors.h"

#define	REQ_READ	1
#define REQ_WRITE	2
#define REQ_QUIT	3

#define CLIENT_NUMBER	4

#define	PROMPT_MAX	32
#define	TEXT_MAX	128

typedef struct request_tag {
	// point to next request
	struct request_tag			*next;
	// operation type
	int					operation;
	// 1 synchronous, 0 nonsynchronous
	int					synchronous;
	int					done_flag;
	// predicate wait when done_flag == 0
	pthread_cond_t				done;
	char					prompt[PROMPT_MAX];
	char					text[TEXT_MAX];
} request_t;

typedef struct tty_server_tag {
	// request linked list
	request_t				*first;
	request_t				*last;
	// whether the server thread is running
	int					running;
	pthread_mutex_t				mutex;
	// predicate wait when first == NULL
	pthread_cond_t				request;
} tty_server_t;

static tty_server_t server = {
	NULL,
	NULL,
	0,
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_COND_INITIALIZER,
};

// main thread use these to wait all client to quit
int client_thread;
static pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t client_cond = PTHREAD_COND_INITIALIZER;

void *server_routine(void *arg)
{
	int status, len;
	request_t *request;
	while (1) {
		status = pthread_mutex_lock(&server.mutex);
		if (status != 0) {
			err_abort(status, "Lock server mutex");
		}

		// wait until there is a request
		while (server.first == NULL) {
			status = pthread_cond_wait(&server.request, &server.mutex);
			if (status != 0) {
				err_abort(status, "Wait on server request cond");
			}
		}

		// get request from linked list
		request = server.first;
		server.first = server.first->next;
		if (server.first == NULL) {
			server.last = NULL;
		}

		status = pthread_mutex_unlock(&server.mutex);
		if (status != 0) {
			err_abort(status, "Unlock server mutex");
		}

		// process request
		switch (request->operation) {
			case REQ_READ:
				if (strlen(request->prompt) > 0) {
					printf("%s\n", request->prompt);
				}
				if (fgets(request->text, TEXT_MAX, stdin) == NULL) {
					request->text[0] = '\0';
				}

				// remove newline
				len = strlen(request->text);
				if (len > 0 && request->text[len - 1] == '\n') {
					request->text[len - 1] = '\0';
				}
				break;

			case REQ_WRITE:
				if (strlen(request->text) > 0) {
					puts(request->text);
				}
				break;

			case REQ_QUIT:
			default:
				break;

		}

		if (request->synchronous) {
			status = pthread_mutex_lock(&server.mutex);
			if (status != 0) {
				err_abort(status, "Lock server mutex");
			}
			request->done_flag = 1;
			status = pthread_cond_signal(&request->done);
			if (status != 0) {
				err_abort(status, "Signal request cond");
			}
			status = pthread_mutex_unlock(&server.mutex);
			if (status != 0) {
				err_abort(status, "Unlock server mutex");
			}
		}
		else {
			free(request);
		}

		if (request->operation == REQ_QUIT) {
			break;
		}
	}
	return NULL;
}

void tty_server_request(int operation, int sync, const char *prompt, char *string)
{
	int status;
	request_t *request;

	status = pthread_mutex_lock(&server.mutex);
	if (status != 0) {
		err_abort(status, "Lock server mutex");
	}

	// check if server thread is already running
	if (!server.running) {
		pthread_t thread;
		pthread_attr_t detached_att;

		status = pthread_attr_init(&detached_att);
		if (status != 0) {
			err_abort(status, "Init detached attribute");
		}

		// set detached attribute for server thread
		status = pthread_attr_setdetachstate(&detached_att, PTHREAD_CREATE_DETACHED);
		if (status != 0) {
			err_abort(status, "Set detach state");
		}

		status = pthread_create(&thread, &detached_att, server_routine, NULL);
		if (status != 0) {
			err_abort(status, "Create server routine");
		}
		server.running = 1;

		status = pthread_attr_destroy(&detached_att);
		if (status != 0) {
			fprintf(stderr, "Destroy detached attribute");
		}
	}

	// add request
	request = malloc(sizeof(request_t));
	if (request == NULL) {
		errno_abort("Allocate memory for request");
	}

	request->next = NULL;
	request->operation = operation;
	request->synchronous = sync;

	// only init condition variable for sync request
	if (sync) {
		request->done_flag = 0;
		pthread_cond_init(&request->done, NULL);
	}

	if (prompt != NULL) {
		strncpy(request->prompt, prompt, PROMPT_MAX);
	}
	else {
		request->prompt[0] = '\0';
	}

	if (operation == REQ_WRITE && string != NULL) {
		strncpy(request->text, string, TEXT_MAX);
	}
	else {
		request->text[0] = '\0';
	}

	if (server.first == NULL) {
		server.first = server.last = request;
	}
	else {
		server.last->next = request;
		server.last = request;
	}

	pthread_cond_signal(&server.request);

	// destory condition variable for sync request
	if (sync) {
		while (!request->done_flag) {
			status = pthread_cond_wait(&request->done, &server.mutex);
			if (status != 0) {
				err_abort(status, "Wait on request done cond");
			}
		}

		if (operation == REQ_READ) {
			if (strlen(request->text) > 0) {
				strncpy(string, request->text, TEXT_MAX);
			}
		}

		status = pthread_cond_destroy(&request->done);
		if (status != 0) {
			err_abort(status, "Destroy request done cond");
		}
		free(request);
	}

	status = pthread_mutex_unlock(&server.mutex);
	if (status != 0) {
		err_abort(status, "Unlock server mutex");
	}
}

void *client_routine(void *args)
{
	int id = (int)args, i, status;
	char prompt[PROMPT_MAX];
	char text[TEXT_MAX], formatted[TEXT_MAX];

	sprintf(prompt, "Client %d>\n", id);
	tty_server_request(REQ_READ, 1, prompt, text);
	for (i = 0; i < 4; ++i) {
		if (strlen(text) == 0) {
			break;
		}

		sprintf(formatted, "(%d#%d) %s\n", id, i, text);
		tty_server_request(REQ_WRITE, 0, NULL, formatted);
		sleep(1);
	}

	status = pthread_mutex_lock(&client_mutex);
	if (status != 0) {
		err_abort(status, "Lock client mutex");
	}

	client_thread--;
	status = pthread_cond_signal(&client_cond);
	if (status != 0) {
		err_abort(status, "Signal client thread quit");
	}


	status = pthread_mutex_unlock(&client_mutex);
	if (status != 0) {
		err_abort(status, "Unlock client mutex");
	}
	return NULL;
}

int main()
{
	int status, i;
	pthread_t thread;

	// create client thread
	client_thread = CLIENT_NUMBER;
	for (i = 0; i < client_thread; ++i) {
		status = pthread_create(&thread, NULL, client_routine, (void *)i);
		if (status != 0) {
			err_abort(status, "Create client thread");
		}
	}

	// wait client thread quit
	status = pthread_mutex_lock(&client_mutex);
	if (status != 0) {
		err_abort(status, "Lock client mutex");
	}

	while (client_thread > 0) {
		status = pthread_cond_wait(&client_cond, &client_mutex);
		if (status != 0) {
			err_abort(status, "Wait client condition");
		}
	}

	status = pthread_mutex_unlock(&client_mutex);
	if (status != 0) {
		err_abort(status, "Unlock client mutex");
	}

	printf("All client thread quit\n");

	// quit server thread
	tty_server_request(REQ_QUIT, 1, NULL, NULL);
	return 0;
}
