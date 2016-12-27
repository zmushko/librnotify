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

	char* dirs[2] = {argv[1], NULL};
	printf("Start to watch %s page_size=%ld max_memory_pages=%ld\n", dirs[0], page_size, max_memory_pages);

	uint32_t mask = IN_ALL_EVENTS;
	//uint32_t mask = IN_MODIFY | IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF | IN_MOVED_FROM | IN_MOVED_TO;

	Notify* ntf = initNotify(dirs, mask/*, "^\\."*/);
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
			printf("path=NULL\n");
			//exit(EXIT_FAILURE);
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
			printf("delete self %s\n", path);
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
			//exit(-1);
		}
		if (!strlen(path))
		{
			printf("path=0\n");
			exit(-1);
		}
		free(path);
	}
	
	freeNotify(ntf);

	return 0;
}

