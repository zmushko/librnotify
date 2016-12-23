/**
 * https://github.com/zmushko/liblst
 * use it as you want but keep this header
 */
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <regex.h>
#include <dirent.h>
#include <stddef.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <ctype.h>
#include <limits.h>

ssize_t lstPush(char*** lst, const char* str)
{
	if (lst == NULL
		|| str == NULL)
	{
	        errno = EINVAL;
		return -1;
	}

	size_t len_ptr	= sizeof(void*);
        size_t i	= 0;

	if (*lst == NULL)
	{
		*lst = (char**)malloc(len_ptr);
		if (*lst == NULL)
		{
			return -1;
		}
		(*lst)[0] = NULL;
	}
	else
	{
		while ((*lst)[i])
		{
			i++;
		}
	}

	(*lst)[i] = (char*)malloc(strlen(str) + 1);
	if ((*lst)[i] == NULL)
	{
		return -1;
	}
	strcpy((*lst)[i], str);

	char** t = (char**)realloc(*lst, len_ptr*(i + 2));
	if (t == NULL)
	{
		free((*lst)[i]);
		(*lst)[i] = NULL;
		return -1;
	}

	*lst		= t;
	(*lst)[i + 1]	= NULL;

	return i;
}

void lstFree(char** lst)
{
	if (lst == NULL)
	{
		return;
	}

        size_t i = 0;
        while (lst[i])
	{
            free(lst[i]);
	    i++;
	}
	free(lst);
}

char** lstReadDir(const char* path)
{
	if (path == NULL)
	{
		errno = EINVAL;
		return NULL;
	}

	DIR* dp	= opendir(path);
	if (dp == NULL)
	{
		return NULL;
	}

	size_t dirent_sz	= offsetof(struct dirent, d_name) + pathconf(path, _PC_NAME_MAX);
	struct dirent* entry	= (struct dirent*)malloc(dirent_sz + 1);
	if (entry == NULL)
	{
		closedir(dp);
		return NULL;
	}

	char** lst = NULL;
	for (;;)
	{
		struct dirent* result = NULL;
		if (0 != readdir_r(dp, entry, &result)
			|| result == NULL)
		{
			break;
		}

                if (!strcmp(entry->d_name, ".")
			|| !strcmp(entry->d_name, ".."))
		{
                        continue;
		}

		if (-1 == lstPush(&lst, result->d_name))
		{
			lstFree(lst);
			free(entry);
			closedir(dp);
			return NULL;
		}
	}

	free(entry);
	closedir(dp);

	return lst;
}
