CC=gcc
CFLAGS=-g -Wall -std=c99 -DDEBUG -D_XOPEN_SOURCE=500
LDFLAGS=-lpthread

SOURCES=alarm.c	alarm_fork.c	alarm_thread.c\
	thread_exit.c	lifecycle.c	alarm_mutex.c\
	trylock.c	backoff.c	cond.c	alarm_cond.c\
	pipe.c		crew.c	server.c

PROGRAMS=$(SOURCES:.c=)

all:	${PROGRAMS}

% : %.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	@rm -rf $(PROGRAMS) *.o
recompile:	clean all
