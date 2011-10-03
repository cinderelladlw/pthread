CC=gcc
CFLAGS=-g -Wall -std=c99 -DDEBUG
LDFLAGS=-lpthread

SOURCES=alarm.c	alarm_fork.c	alarm_thread.c\
	alarm_mutex.c
PROGRAMS=$(SOURCES:.c=)

all:	${PROGRAMS}

%.o : %c
	$(CC) $(CFLAGS) $< -o $@

% : %.o
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@
clean:
	@rm -rf $(PROGRAMS) *.o
recompile:	clean all
