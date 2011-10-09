#include <pthread.h>
#include <stddef.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "errors.h"

#define	CREW_SIZE	4

typedef struct work_tag {
	struct work_tag			*next;
	char				*path;
	char				*search;
}work_t, *work_p;

typedef struct worker_tag {
	pthread_t			thread;
	// index in crew
	int				index;
	// point back to crew
	struct crew_tag			*crew;
}worker_t, *worker_p;

typedef struct crew_tag {
	int				crew_size;
	worker_t			worker[CREW_SIZE];
	int				work_count;
	work_p				first;
	work_p				last;
	// protect access to crew
	pthread_mutex_t			mutex;
	// predicate work_count == 0, there is no more work in crew
	pthread_cond_t			done;
	// predicate work_count > 0, there is more work in crew
	pthread_cond_t			go;
}crew_t, *crew_p;

size_t path_max;
size_t name_max;

void *worker_routine(void *arg)
{
	worker_p mine = (worker_p)arg;
	work_p work, new_work;
	crew_p crew = mine->crew;
	struct stat filestat;

	int status;

	// allocate enough memory for struct dirent
	// POSIX does not specify the size of d_name field, but requires d_name is the LAST field in struct dirent
	size_t len = offsetof(struct dirent, d_name) + name_max;
	struct dirent *entry = malloc(len);
	if (entry == NULL) {
		errno_abort("Allocate memory for struct dirent");
	}

	// when thread start, the work_count == 0, so wait until there is works to do
	status = pthread_mutex_lock(&crew->mutex);
	if (status != 0) {
		err_abort(status, "Lock crew mutex");
	}

	DPRINTF(("worker %d: wait work on (crew->work_count == 0), crew->work_count %d\n", mine->index, crew->work_count));
	while (crew->work_count == 0) {
		status = pthread_cond_wait(&crew->go, &crew->mutex);
		if (status != 0) {
			err_abort(status, "Wait on go cond for more work");
		}
	}

	status = pthread_mutex_unlock(&crew->mutex);
	if (status != 0) {
		err_abort(status, "Unlock crew mutex");
	}

	DPRINTF(("worker %d: start to work\n", mine->index));


	// get work item from crew, decrement the work_count AFTER deal with it,
	// if crew->first == NULL, then there is no work temporary
	// if crew->work_count <= 0, then the work of crew is done
	while (1) {
		// get work item from crew
		status = pthread_mutex_lock(&crew->mutex);
		if (status != 0) {
			err_abort(status, "Lock crew mutex");
		}

		DPRINTF(("worker %d: wait work on (crew->first == NULL) work count %d\n", mine->index, crew->work_count));
		while (crew->first == NULL) {
			status = pthread_cond_wait(&crew->go, &crew->mutex);
			if (status != 0) {
				err_abort(status, "Wait on go cond for more work");
			}
		}

		// get work item from crew
		work = crew->first;
		DPRINTF(("worker %d: get work %p, work count %d, work path %s\n", mine->index, work, crew->work_count, work->path));
		crew->first = work->next;
		if (crew->first == NULL) {
			crew->last = NULL;
		}

		status = pthread_mutex_unlock(&crew->mutex);
		if (status != 0) {
			err_abort(status, "Unlock crew mutex");
		}

		// precess work item
		status = lstat(work->path, &filestat);
		if (status != 0) {
			errno_abort("lstat error");
		}

		// link file
		if (S_ISLNK(filestat.st_mode)) {
			printf("OUTPUT: worker %d: don't follow link %s\n", mine->index, work->path);
		}
		// directory file
		else if (S_ISDIR(filestat.st_mode)) {
			DIR *dir;
			struct dirent *result;
			dir = opendir(work->path);
			if (dir == NULL) {
				fprintf(stderr, "OUTPUT: worker %d: Can't open directory %s, %d(%s)\n", mine->index, work->path, errno, strerror(errno));
				continue;
			}

			while (1) {
				status = readdir_r(dir, entry, &result);
				if (status != 0) {
					fprintf(stderr, "OUTPUT: worker %d: Can't read directory %s, %d(%s)\n", mine->index, work->path, errno, strerror(errno));
					break;
				}
				// end of directory stream
				if (result == NULL) {
					break;
				}

				if (strcmp(entry->d_name, ".") == 0
						|| strcmp(entry->d_name, "..") == 0) {
					continue;
				}

				new_work = malloc(sizeof(work_t));
				if (new_work == NULL) {
					errno_abort("Allocate memory for new work");
				}
				new_work->path = malloc(path_max);
				if (new_work->path == NULL) {
					errno_abort("Allocate memory for new work's path");
				}
				strcpy(new_work->path, work->path);
				strcat(new_work->path, "/");
				strcat(new_work->path, entry->d_name);

				new_work->search = work->search;
				new_work->next = NULL;

				status = pthread_mutex_lock(&crew->mutex);
				if (status != 0) {
					err_abort(status, "Lock crew mutex");
				}

				if (crew->first == NULL) {
					crew->first = crew->last = new_work;
				} else {
					crew->last->next = new_work;
					crew->last = new_work;
				}
				crew->work_count++;
				DPRINTF(("worker %d: add work, work %p work count %d, work path %s\n", mine->index, new_work, crew->work_count, new_work->path));
				status = pthread_cond_signal(&crew->go);
				if (status != 0) {
					err_abort(status, "Signal go cond after insert new work item");
				}

				status = pthread_mutex_unlock(&crew->mutex);
				if (status != 0) {
					err_abort(status, "Unlock crew mutex");
				}

			}

			status = closedir(dir);
			if (status != 0) {
				fprintf(stderr, "OUTPUT: worker %d: Can't close directory %s, %d(%s)\n", mine->index, work->path, errno, strerror(errno));
			}
		}
		// regular file
		else if (S_ISREG(filestat.st_mode)) {
			FILE *file;
			char buffer[256];
			char *bufferptr, *search;

			file = fopen(work->path, "r");
			if (file == NULL) {
				fprintf(stderr, "OUTPUT: worker %d: Can't read file %s, %d(%s)\n", mine->index, work->path, errno, strerror(errno));
				continue;
			}

			while (1) {
				bufferptr = fgets(buffer, sizeof(buffer), file);
				if (bufferptr == NULL) {
					if (ferror(file)) {
						fprintf(stderr, "OUTPUT: worker %d: Can't read file %s, %d(%s)\n", mine->index, work->path, errno, strerror(errno));
						break;
					}

					if (feof(file)) {
						break;
					}
				}

				search = strstr(buffer, work->search);
				if (search != NULL) {
					printf("OUTPUT: worker %d: find %s from %s\n", mine->index, work->search, work->path);
					break;
				}
			}

			status = fclose(file);
			if (status != 0) {
				fprintf(stderr, "OUTPUT: worker %d: Can't close file %s, %d(%s)\n", mine->index, work->path, errno, strerror(errno));
			}
		}
		else {
			fprintf(stderr, "OUTPUT: worker %d: %s file type is %d(%s)\n", mine->index, work->path, filestat.st_mode & S_IFMT,
					S_ISFIFO(filestat.st_mode) ? "FIFO"
					: S_ISCHR(filestat.st_mode) ? "CHR"
					: S_ISBLK(filestat.st_mode) ? "BLK"
					: S_ISSOCK(filestat.st_mode) ? "SOCK"
					: "UNKNOWN");
		}

		DPRINTF(("worker %d: finish work %p, work count %d, work path %s\n", mine->index, work, crew->work_count, work->path));
		free(work->path);
		free(work);

		// decrement work count
		status = pthread_mutex_lock(&crew->mutex);
		if (status != 0) {
			err_abort(status, "Lock crew mutex");
		}

		--crew->work_count;
		DPRINTF(("worker %d: decrement work count %d\n", mine->index, crew->work_count));
		if (crew->work_count == 0) {
			status = pthread_cond_signal(&crew->done);
			if (status != 0) {
				err_abort(status, "Signal done cond");
			}
			status = pthread_mutex_unlock(&crew->mutex);
			if (status != 0) {
				err_abort(status, "Unlock crew mutex");
			}
			break;
		}
		status = pthread_mutex_unlock(&crew->mutex);
		if (status != 0) {
			err_abort(status, "Unlock crew mutex");
		}
		sched_yield();
	}

	free(entry);
	return NULL;
}

int create_crew(crew_p crew, int crew_size)
{
	int status, i;

	if (crew_size > CREW_SIZE) {
		return EINVAL;
	}

	crew->crew_size = crew_size;
	crew->work_count = 0;
	crew->first = crew->last = NULL;

	status = pthread_mutex_init(&crew->mutex, NULL);
	if (status != 0) {
		return status;
	}
	status = pthread_cond_init(&crew->go, NULL);
	if (status != 0) {
		return status;
	}
	status = pthread_cond_init(&crew->done, NULL);
	if (status != 0) {
		return status;
	}

	for (i = 0; i < CREW_SIZE; ++i) {
		crew->worker[i].index = i;
		crew->worker[i].crew = crew;
		status = pthread_create(&crew->worker[i].thread, NULL, worker_routine, &crew->worker[i]);
		if (status != 0) {
			err_abort(status, "Create worker");
		}
	}

	return 0;
}

int crew_start(crew_p crew, const char *path, char *search)
{
	int status;
	work_p work;

	status = pthread_mutex_lock(&crew->mutex);
	if (status != 0) {
		return status;
	}

	// if crew is busy, then wait
	while (crew->work_count > 0) {
		status = pthread_cond_wait(&crew->done, &crew->mutex);
		if (status != 0) {
			pthread_mutex_unlock(&crew->mutex);
			return status;
		}
	}

	errno = 0;
	path_max = pathconf(path, _PC_PATH_MAX);
	if (path_max == -1) {
		if (errno == 0) {
			path_max = 1024;
		}
		else {
			errno_abort("Unable to get _PC_PATH_MAX");
		}
	}

	errno = 0;
	name_max = pathconf(path, _PC_NAME_MAX);
	if (name_max == -1) {
		if (errno == 0) {
			name_max = 256;
		}
		else {
			errno_abort("Unable to get _PC_NAME_MAX");
		}
	}

	// plus 1 for terminal '\0'
	++path_max;
	++name_max;

	work = malloc(sizeof(work_t));
	if (work == NULL) {
		errno_abort("Allocate memory for new work");
	}
	work->path = malloc(path_max);
	if (work->path == NULL) {
		errno_abort("Allocate memory for new work's path");
	}

	strcpy(work->path, path);
	work->search = search;
	work->next = NULL;

	if (crew->first == NULL) {
		crew->first = crew->last = work;
	}
	else {
		crew->last->next = work;
		crew->last = work;
	}
	++crew->work_count;

	status = pthread_cond_signal(&crew->go);
	if (status != 0) {
		free(work->path);
		free(work);
		crew->first = crew->last = NULL;
		crew->work_count = 0;
		pthread_mutex_unlock(&crew->mutex);
		return status;
	}

	while (crew->work_count > 0) {
		status = pthread_cond_wait(&crew->done, &crew->mutex);
		if (status != 0) {
			err_abort(status, "Wait on cond crew done");
		}
	}

	status = pthread_mutex_unlock(&crew->mutex);
	if (status != 0) {
		err_abort(status, "Unlock crew mutex");
	}

	return 0;
}


int main(int argc, char **argv)
{
	int status;
	crew_t crew;

	if (argc < 3) {
		fprintf(stderr, "%s path string\n", argv[0]);
		return -1;
	}

	status = create_crew(&crew, CREW_SIZE);
	if (status != 0) {
		err_abort(status, "Create crew");
	}

	status = crew_start(&crew, argv[1], argv[2]);
	if (status != 0) {
		err_abort(status, "Crew start");
	}

	return 0;
}
