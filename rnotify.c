#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "rnotify.h"

#define PATH_MAX_QUEUED_EVENTS	"/proc/sys/fs/inotify/max_queued_events"

typedef struct {
	int fd;
	unsigned int size_w;
	char** w;
	long max_name;
	unsigned long max_queued_events;
	uint32_t mask;
	regex_t* exclude;
	struct chainEvent* head;
	struct chainEvent* tail;
	struct Cookie* cookies;
} Notify;

struct chainEvent
{
	struct chainEvent* prev;
	struct inotify_event* e;
	struct chainEvent* next;
};

struct Cookie {
	struct Cookie* prev;
	uint32_t cookie;
	char* path;
	char* name;
	struct Cookie* next;
};

static void* Malloc(size_t size)
{
	void* new_mem = (void*)malloc(size);
	if (new_mem == NULL)
	{
		fprintf(stderr, "fatal error: malloc()\n");
		exit(EXIT_FAILURE);
	}
	return new_mem;
}

static int addCookie(struct Cookie** p, const char* path, const char* name, uint32_t cookie)
{
	struct Cookie* new_p = (struct Cookie*)Malloc(sizeof(struct Cookie));
	memset(new_p, 0, sizeof(struct Cookie));

	new_p->path = (char*)Malloc(strlen(path) + 1);
	strcpy(new_p->path, path);

	new_p->name = (char*)Malloc(strlen(name) + 1);
	strcpy(new_p->name, name);

	new_p->cookie = cookie;

	new_p->prev = NULL;
	new_p->next = NULL;

	if (*p == NULL)
	{
		*p = new_p;
		return 0;
	}


	struct Cookie* c = *p;
	while (c->next)
	{
		c = c->next;
	}

	c->next = new_p;
	new_p->prev = c;
	return 0;
}

static struct Cookie* getCookie(struct Cookie** head, uint32_t cookie)
{
	struct Cookie* c = *head;
	while (c)
	{
		if (c->cookie == cookie)
		{
			if (c->prev)
			{
				c->prev->next = c->next;
			}
			else
			{
				*head = c->next;
			}
			if (c->next)
			{
				c->next->prev = c->prev;
			}
			return c;
		}
		c = c->next;
	}
	return NULL;
}

static void freeCookie(struct Cookie* p)
{
	free(p->name);
	free(p->path);
	free(p);
}

static ssize_t lstPush(char*** lst, const char* str)
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
		(*lst)[i]	= NULL;
		return -1;
	}

	*lst		= t;
	(*lst)[i + 1]	= NULL;

	return i;
}

static void lstFree(char** lst)
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

static char** lstReadDir(const char* path)
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

static int pushChainEvent(Notify* ntf, struct inotify_event* e)
{
	if (ntf == NULL || e == NULL)
	{
		errno = EINVAL;
		return -1;
	}

	if (ntf->exclude && !regexec(ntf->exclude, e->name, 0, NULL, 0))
	{
		return 0;
	}

	size_t e_size = sizeof(struct inotify_event) + e->len;
	struct inotify_event* event = (struct inotify_event*)malloc(e_size);
	if (event == NULL)
	{
		return -1;
	}
	memcpy(event, e, e_size);
	struct chainEvent* element = (struct chainEvent*)malloc(sizeof(struct chainEvent));
	if (element == NULL)
	{
		free(event);
		return -1;
	}
	element->e = event;
	element->next = ntf->tail;
	element->prev = NULL;
	if (ntf->tail != NULL)
	{
		ntf->tail->prev = element;
	}
	ntf->tail = element;
	if (ntf->head == NULL)
	{
		ntf->head = element;
	}
	return 0;
}

static struct inotify_event* outChainEvent(Notify* ntf)
{
	if (ntf->head == NULL)
	{
		return NULL;
	}
	struct inotify_event* event = ntf->head->e;
	struct chainEvent* prev = ntf->head->prev;
	free(ntf->head);
	ntf->head = prev;
	if (prev == NULL)
	{
		ntf->tail = NULL;
	}
	return event;
}

static inline int isDir(const char* path)
{
	struct stat sb;
	if (-1 == stat(path, &sb))
	{
		return -1;
	}

	if (S_ISDIR(sb.st_mode))
	{
		return 1;
	}

	return 0;
}

static int addNotify(Notify* ntf, const char* path, uint32_t cookie)
{
	uint32_t mask	= ntf->mask;
	/* Hack */
	if (strstr(path, "/" HIDDEN_UPLOAD_DIR))
	{
		mask = IN_CREATE | IN_MOVED_TO;
	}

	int wd = inotify_add_watch(ntf->fd, path, ntf->mask /*mask*/ /*11.03.14+Hack ntf->mask*/);
	if (-1 == wd)
	{
		return -1;
	}

	if (wd > ntf->size_w)
	{
		char** t = (char**)realloc(ntf->w, (sizeof(void*))*(wd));
		if (t == NULL)
		{
			return -1;
		}
		ntf->w = t;
		size_t i = 0;
		for (i = ntf->size_w; i < wd; i++)
		{
			ntf->w[i] = NULL;
		}
		ntf->size_w = wd;
	}

	if (ntf->w == NULL) // for [censored] Coverity
	{
		return -1;
	}

	if (ntf->w[wd - 1] != NULL)
	{
		free(ntf->w[wd - 1]);
	}

	ntf->w[wd - 1] = (char*)malloc(strlen(path) + 1);
	if (ntf->w[wd - 1] == NULL)
	{
		return -1;
	}
	strcpy(ntf->w[wd - 1], path);

	char** elems = lstReadDir(path);
	if (elems == NULL)
	{
		return 0;
	}

	size_t i = 0;
	while (elems[i])
	{
		char* path_elem = (char*)malloc(strlen(path) + strlen(elems[i]) + 2);
		if (path_elem == NULL)
		{
			lstFree(elems);
			return -1;
		}
		sprintf(path_elem, "%s/%s", path, elems[i]);
		int is_dir = isDir(path_elem);
		if (-1 == is_dir)
		{
			free(path_elem);
			lstFree(elems);
			return -1;
		}
		struct inotify_event* e = (struct inotify_event*)malloc(sizeof(struct inotify_event) + strlen(elems[i]) + 1);
		if (e == NULL)
		{
			free(path_elem);
			lstFree(elems);
			return -1;
		}
		memset(e, 0, sizeof(struct inotify_event));
		e->wd = wd;
		if (is_dir)
		{
			e->mask = IN_CREATE | IN_ISDIR;
		}
		else
		{
			e->mask = IN_CREATE;
		}
		e->len = strlen(elems[i]) + 1;
		e->cookie = cookie;
		strcpy(e->name, elems[i]);
		if (-1 == pushChainEvent(ntf, e))
		{
			free(e);
			free(path_elem);
			lstFree(elems);
			return -1;
		}
		free(e);
		if (!is_dir)
		{
			e = (struct inotify_event*)malloc(sizeof(struct inotify_event) + strlen(elems[i]) + 1);
			if (e == NULL)
			{
				free(path_elem);
				lstFree(elems);
				return -1;
			}
			memset(e, 0, sizeof(struct inotify_event));
			e->wd = wd;
			e->mask = IN_CLOSE_WRITE;
			e->len = strlen(elems[i]) + 1;
			strcpy(e->name, elems[i]);
			if (-1 == pushChainEvent(ntf, e))
			{
				free(e);
				free(path_elem);
				lstFree(elems);
				return -1;
			}
			free(e);
		}
		free(path_elem);
		i++;
	}

	lstFree(elems);

	return 0;
}

Notify* initNotify(const char* path, const uint32_t mask, const char* exclude)
{
	if (path == NULL)
	{
		errno = EINVAL;
		return NULL;
	}

	size_t size	= sizeof(Notify);
	Notify* ntf	= (Notify*)malloc(size);
	if (ntf == NULL)
	{
		return NULL;
	}
	memset(ntf, 0, size);
	ntf->mask	= mask;
	ntf->size_w	= 0;
	ntf->w		= NULL;
	ntf->head	= NULL;
	ntf->tail	= NULL;
	ntf->cookies	= NULL;

	ntf->max_name = pathconf(path, _PC_NAME_MAX);
	if (-1 == ntf->max_name)
	{
		free(ntf);
		return NULL;
	}

	if (exclude == NULL)
	{
		ntf->exclude = NULL;
	}
	else
	{
		regex_t* preg = (regex_t*)malloc(sizeof(regex_t));
		if (preg == NULL)
		{
			free(ntf);
			return NULL;
		}
		memset(preg, 0, sizeof(regex_t));
		if (0 != regcomp(preg, exclude, REG_EXTENDED))
		{
			free(ntf);
			free(preg);
			return NULL;
		}
		ntf->exclude = preg;
	}

	unsigned long max_queued_events = 0;
	FILE* f = fopen(PATH_MAX_QUEUED_EVENTS, "r");
	if (f == NULL)
	{
		if (ntf->exclude)
		{
			regfree(ntf->exclude);
			free(ntf->exclude);
		}
		free(ntf);
		return NULL;
	}
	if (1 != fscanf(f, "%10lu", &max_queued_events))
	{
		if (ntf->exclude)
		{
			regfree(ntf->exclude);
			free(ntf->exclude);
		}
		if (fclose(f))
		{
			errno = 0;
		}
		free(ntf);
		return NULL;
	}
	ntf->max_queued_events = max_queued_events;
	if (fclose(f))
	{
		errno = 0;
	}

	ntf->fd	= inotify_init();
	if (-1 == ntf->fd)
	{
		if (ntf->exclude)
		{
			regfree(ntf->exclude);
			free(ntf->exclude);
		}
		free(ntf);
		return NULL;
	}

	if (-1 == addNotify(ntf, path, 0))
	{
		if (ntf->exclude)
		{
			regfree(ntf->exclude);
			free(ntf->exclude);
		}
		if (close(ntf->fd))
		{
			errno = 0;
		}
		free(ntf);
		return NULL;
	}

	return ntf;
}

static int renameWatches(Notify* ntf, const char* oldpath, const char* newpath)
{
	unsigned int i = 0;
	for (; i < ntf->size_w; ++i)
	{
		if (ntf->w[i] == NULL)
		{
			continue;
		}
		if (ntf->w[i] == strstr(ntf->w[i], oldpath)
			&& (strlen(ntf->w[i]) == strlen(oldpath)
				|| *(ntf->w[i] + strlen(oldpath)) == '/' ))
		{
			char* p = (char*)Malloc(snprintf(NULL, 0, "%s%s", newpath, ntf->w[i] + strlen(oldpath)) + 1);
			sprintf(p, "%s%s", newpath, ntf->w[i] + strlen(oldpath));
			free(ntf->w[i]);
			ntf->w[i] = p;
		}
	}
	return 0;
}

int waitNotify(Notify* ntf, char** const path, uint32_t* mask, const int timeout, uint32_t* cookie)
{
	if (ntf == NULL
		|| path	== NULL)
	{
		errno = EINVAL;
		return -1;
	}

	struct inotify_event* e = NULL;
	while (NULL == (e = outChainEvent(ntf)))
	{
		fd_set set;
		FD_ZERO(&set);
		FD_SET(ntf->fd, &set);
		struct timeval t = { 0, timeout };
		int rd = select(ntf->fd + 1, &set, NULL, NULL, &t);
		if (!rd)
		{
			return timeout;
		}
		if (rd < 0)
		{
			return -1;
		}

		int event_size = sizeof(struct inotify_event);
		size_t buf_size = 0;
		if (-1 == ioctl(ntf->fd, FIONREAD, &buf_size))
		{
			return -1;
		}
		buf_size *= event_size + ntf->max_name + 1;
		char* buffer = (char*)malloc(buf_size);
		if (buffer == NULL)
		{
			return -1;
		}
		memset(buffer, 0, buf_size);

		int safe_errno = errno;
		ssize_t length = 0;
		while (-1 == (length = read(ntf->fd, buffer, buf_size)))
		{
			if (errno == EINTR
				|| errno == EAGAIN
				|| errno == EWOULDBLOCK)
			{
				errno = safe_errno;
				continue;
			}
			*path = NULL;
			free(buffer);
			return -1;
		}

		size_t i = 0;
		while (i < length)
		{
			e = (struct inotify_event*)&buffer[i];
			if (-1 == pushChainEvent(ntf, e))
			{
				free(buffer);
				return -1;
			}
			i += event_size + e->len;
		}
		free(buffer);
	}

	*path = strdup("");

	if (e->wd > 0)
	{
		char* path_watch = ntf->w[e->wd - 1];
		if (path_watch)
		{
			free(*path);
			if (e->len)
			{
				*path = (char*)Malloc(strlen(path_watch) + strlen(e->name) + 2);
				sprintf(*path, "%s/%s", path_watch, e->name);
			}
			else
			{
				*path = (char*)Malloc(strlen(path_watch) + 1);
				strcpy(*path, path_watch);
			}
		}
	}

	if (mask)
	{
		*mask = e->mask;
	}

	if (e->mask & IN_CREATE
		&& e->mask & IN_ISDIR)
	{
		if (-1 == addNotify(ntf, *path, 0))
		{
			free(e);
			return -1;
		}
	}

	if (e->mask & IN_MOVED_FROM
		&& e->mask & IN_ISDIR)
	{
		if (-1 == addCookie(&ntf->cookies, ntf->w[e->wd - 1], e->name, e->cookie))
		{
			free(e);
			return -1;
		}
	}

	if (e->mask & IN_MOVED_TO
		&& e->mask & IN_ISDIR)
	{
		struct Cookie* C = getCookie(&ntf->cookies, e->cookie);
		if (C)
		{
			char* oldpath = (char*)Malloc(snprintf(NULL, 0, "%s/%s", C->path, C->name) + 1);
			sprintf(oldpath, "%s/%s", C->path, C->name);

			char* newpath = (char*)Malloc(snprintf(NULL, 0, "%s/%s", ntf->w[e->wd - 1], e->name) + 1);
			sprintf(newpath, "%s/%s", ntf->w[e->wd - 1], e->name);

			renameWatches(ntf, oldpath, newpath);

			if (-1 == addNotify(ntf, newpath, 0))
			{
				free(oldpath);
				free(newpath);
				freeCookie(C);
				free(e);
				return -1;
			}
			free(oldpath);
			free(newpath);
			freeCookie(C);
		}
	}

	if (e->mask & IN_IGNORED)
	{
		free(ntf->w[e->wd - 1]);
		ntf->w[e->wd - 1] = NULL;
	}

	*cookie = e->cookie;
	free(e);

	return 0;
}

void freeNotify(Notify* ntf)
{
	if (ntf == NULL)
	{
		return;
	}
	int safe_errno = errno;
	struct inotify_event* e = NULL;
	while (NULL != (e = outChainEvent(ntf)))
	{
		free(e);
	}
	size_t i = 0;
	for (i = 0; i < ntf->size_w; i++)
	{
		if (ntf->w[i] != NULL)
		{
			free(ntf->w[i]);
		}
		inotify_rm_watch(ntf->fd, i + 1);
	}
	close(ntf->fd);
	if (ntf->exclude)
	{
		regfree(ntf->exclude);
		free(ntf->exclude);
	}
	free(ntf);
	errno = safe_errno;
	return;
}

