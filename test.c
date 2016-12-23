#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "rnotify.h"

#define MAX_MEMORY_SIZE 1000*1024

int main(int argc, char* argv[])
{
	if (argc != 2)
	{
		printf("Usage: a.out <dir>\n");
		exit(1);
	}

	long page_size = sysconf(_SC_PAGESIZE);
	size_t path_to_statm_size = 64;
	char path_to_statm[path_to_statm_size];
	snprintf(path_to_statm, path_to_statm_size, "/proc/%d/statm", getpid());
	const long max_memory_pages = MAX_MEMORY_SIZE / page_size;

	char* dir[2] = {argv[1], NULL};
	printf("Start to watch %s page_size=%ld max_memory_pages=%ld\n", dir[0], page_size, max_memory_pages);

	uint32_t mask = IN_ALL_EVENTS;

	Notify* ntf = initNotify(dir, mask, "^\\.");
	if (ntf == NULL)
	{
		exit(EXIT_FAILURE);
	}

	for (;;)
	{
		char* path = NULL;
		uint32_t mask = 0;
		uint32_t cookie = 0;
		waitNotify(ntf, &path, &mask, 0, &cookie);
		if (path == NULL)
		{
			exit(EXIT_FAILURE);
		}
		if (mask & IN_ATTRIB)
		{
			//printf("attrib \t%s cookie=%d\n", path, cookie);
		}
		if (mask & IN_CLOSE_WRITE)
		{
			//printf("close write \t%s cookie=%d\n", path, cookie);
		}
		if (mask & IN_CLOSE_NOWRITE)
		{
			//printf("close nowrite \t%s cookie=%d\n", path, cookie);
		}
		if (mask & IN_ACCESS)
		{
			//printf("access \t%s cookie=%d\n", path, cookie);
		}
		if (mask & IN_MODIFY)
		{
			//printf("modify \t%s cookie=%d\n", path, cookie);
		}
		if (mask & IN_OPEN)
		{
			//printf("open \t%s cookie=%d\n", path, cookie);
		}
		if (mask & IN_CREATE)
		{
			printf("create \t%s cookie=%d\n", path, cookie);
		}
		if (mask & IN_DELETE)
		{
			printf("delete \t%s cookie=%d\n", path, cookie);
		}
		if (mask & IN_DELETE_SELF)
		{
			//printf("delete self %s\n", path);
		}
		if (mask & IN_MOVE_SELF)
		{
			//printf("move self %s\n", path);
		}
		if (mask & IN_MOVED_FROM)
		{
			printf("moved from \t%s cookie=%d\n", path, cookie);
		}
		if (mask & IN_MOVED_TO)
		{
			printf("moved to \t%s cookie=%d\n", path, cookie);
		}
		if (mask & IN_Q_OVERFLOW)
		{
			printf("overflow\n");
		}
		if (!strlen(path))
		{
			exit(-1);
		}
		free(path);
	}
	
	freeNotify(ntf);

	return 0;
}

