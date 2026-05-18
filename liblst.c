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

/*
 * Append a strdup'd copy of `str` to the NULL-terminated `**lst`.
 * Allocates the list on first call if `*lst` is NULL. Returns the
 * index of the inserted element on success, or -1 on allocation
 * failure / NULL input (errno set to EINVAL or ENOMEM).
 */
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

	size_t str_size = strlen(str) + 1;
	(*lst)[i] = (char*)malloc(str_size);
	if ((*lst)[i] == NULL)
	{
		return -1;
	}
	memcpy((*lst)[i], str, str_size);

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

/* Release a NULL-terminated string list built by lstPush/lstReadDir.
 * Safe to pass NULL. */
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

/*
 * Read `path` and return a NULL-terminated list of its entries
 * (excluding "." and ".."), in opendir/readdir order. Release the
 * result with lstFree.
 *
 * Returns NULL on opendir failure, readdir error, or allocation
 * failure (errno set; opendir/readdir/malloc errnos as is).
 */
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

	char** lst = NULL;
	for (;;)
	{
		errno = 0;
		struct dirent* result = readdir(dp);
		if (result == NULL)
		{
			if (errno != 0)
			{
				perror("Error reading directory");
				lstFree(lst);
				closedir(dp);
				return NULL;
			}
			break;
		}

		if (!strcmp(result->d_name, ".")
			|| !strcmp(result->d_name, ".."))
		{
                        continue;
		}

		if (-1 == lstPush(&lst, result->d_name))
		{
			lstFree(lst);
			closedir(dp);
			return NULL;
		}
	}

	closedir(dp);

	return lst;
}

/*
 * printf-style format that returns a freshly malloc'd string sized
 * by the same formatting (uses vsnprintf twice: once to measure,
 * once to write). Caller frees with free().
 *
 * Returns NULL on EINVAL (NULL format), encoding error from
 * vsnprintf, or allocation failure.
 */
char* lstString(const char* format, ...)
{
	if(format == NULL)
	{
		errno = EINVAL;
		return NULL;
	}

	va_list ap, apt;
	va_start(ap, format);
	va_copy(apt, ap);

	int needed = vsnprintf(NULL, 0, format, apt);
	va_end(apt);
	if (needed < 0)
	{
		va_end(ap);
		return NULL;
	}

	size_t size = (size_t)needed + 1;
	char* rval = (char*)malloc(size);
	if(rval == NULL)
	{
		va_end(ap);
		errno = ENOMEM;
		return NULL;
	}
	vsnprintf(rval, size, format, ap);

	va_end(ap);

	return rval;
}


