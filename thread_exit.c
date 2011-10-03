#include <pthread.h>
#include "errors.h"

void *thread_function(void *arg)
{
	sleep(5);
	printf("thread_function say hi\n");
	return NULL;
}

int main()
{
	pthread_t thread;
	pthread_create(&thread, NULL, thread_function, NULL);
	pthread_exit(NULL);
}
