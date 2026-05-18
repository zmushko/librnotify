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

/**
 * @brief Adds a string to the end of a string list
 *
 * @param lst Pointer to a string list (array of strings)
 * @param str String to be added to the list
 *
 * @return On success, returns the new size of the list after push operation
 *         On failure, returns a negative value
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

/**
 * @brief Frees memory allocated for a list of strings
 *
 * This function deallocates memory used by a list of strings.
 * It assumes the list was allocated dynamically and terminates with a NULL entry.
 *
 * @param lst Pointer to an array of strings (char*) to be freed
 *
 * @note The function expects the list to be NULL-terminated or
 *       to have been created by a corresponding list allocation function.
 */
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

/**
 * @brief Reads the contents of a directory and returns them as an array of strings.
 * 
 * This function scans the specified directory and returns all entries found.
 * 
 * @param path The path to the directory to read
 * @return A null-terminated array of strings (char**) containing the directory entries.
 *         Each string in the array must be freed by the caller.
 *         Returns NULL if the directory couldn't be opened or if memory allocation fails.
 * 
 * @note The caller is responsible for freeing the returned array and all strings it contains.
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

/**
 * @brief Creates a formatted string similar to printf.
 *
 * This function creates a new string by formatting the input according to
 * the specified format string and variable arguments, similar to printf.
 *
 * @param format A printf-style format string.
 * @param ... Variable arguments to be formatted according to the format string.
 *
 * @return A dynamically allocated string containing the formatted output.
 *         The caller is responsible for freeing this memory when no longer needed.
 *
 * @note The returned string is allocated on the heap and must be freed by the caller
 *       to avoid memory leaks.
 *
 * @example
 * char* greeting = lstString("Hello, %s! You are %d years old.", name, age);
 * // use greeting
 * free(greeting);  // Don't forget to free when done
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


