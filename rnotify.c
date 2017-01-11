#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/inotify.h>

#include "liblst.h"
#include "rnotify.h"

struct chainEvent
{
        struct chainEvent* prev;
        struct inotify_event* e;
        struct chainEvent* next;
};

struct Cookie 
{
        struct Cookie* prev;
        uint32_t cookie;
        char* path;
        char* name;
        struct Cookie* next;
};

struct wd_path
{
	unsigned long wd;
	char* path;
};

struct _rnotify 
{
        int fd;
        unsigned int size_w;
        char** w;
        long max_name;
        unsigned long max_queued_events;
        uint32_t mask;
        struct chainEvent* head;
        struct chainEvent* tail;
        struct Cookie* cookies;
};

#define PATH_MAX_QUEUED_EVENTS	"/proc/sys/fs/inotify/max_queued_events"

static int addCookie(struct Cookie** p, const char* path, const char* name, uint32_t cookie)
{
	struct Cookie* new_p = (struct Cookie*)malloc(sizeof(struct Cookie));
	if (new_p == NULL)
	{
		return -1;
	}
	memset(new_p, 0, sizeof(struct Cookie));

	new_p->path = (char*)malloc(strlen(path) + 1);
	if (new_p->path == NULL)
	{
		free(new_p);
		return -1;
	}
	strcpy(new_p->path, path);

	new_p->name = (char*)malloc(strlen(name) + 1);
	if (new_p->name == NULL)
	{
		free(new_p);
		free(new_p->path);
		return -1;
	}
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

static int pushChainEvent(Notify* ntf, struct inotify_event* e)
{
	if (ntf == NULL || e == NULL)
	{
		errno = EINVAL;
		return -1;
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
	element->e	= event;
	element->next	= ntf->tail;
	element->prev	= NULL;
	
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

static struct inotify_event* pullChainEvent(Notify* ntf)
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

static void updateMaxName(Notify* ntf, char* path)
{
	if (!access(path, F_OK))
	{
		long max_name = pathconf(path, _PC_NAME_MAX);
		ntf->max_name = (max_name > ntf->max_name) ? max_name : ntf->max_name;
	}
}

static int addNotify(Notify* ntf, const char* path, uint32_t cookie)
{
	errno = 0;
	int wd = inotify_add_watch(ntf->fd, path, ntf->mask);
	if (-1 == wd)
	{
		if (errno == ENOENT)
		{
			return 0;
		}
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

	if (ntf->w == NULL)
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
		
		struct stat sb;
		int is_dir = (!stat(path, &sb) && S_ISDIR(sb.st_mode)) ? 1 : 0;
		
		int event_size = sizeof(struct inotify_event);
		struct inotify_event* e = (struct inotify_event*)malloc(event_size + strlen(elems[i]) + 1);
		if (e == NULL)
		{
			free(path_elem);
			lstFree(elems);
			return -1;
		}
		memset(e, 0, event_size);

		e->wd = wd;
		if (is_dir)
		{
			e->mask = IN_CREATE | IN_ISDIR;
			updateMaxName(ntf, path_elem);
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
			e = (struct inotify_event*)malloc(event_size + strlen(elems[i]) + 1);
			if (e == NULL)
			{
				free(path_elem);
				lstFree(elems);
				return -1;
			}
			memset(e, 0, event_size);
			
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

Notify* initNotify(char** path, const uint32_t mask)
{
	if (path == NULL || path[0] == NULL)
	{
		errno = EINVAL;
		return NULL;
	}

	size_t i	= 0;
	size_t size	= sizeof(Notify);
	Notify* ntf	= (Notify*)malloc(size);
	if (ntf == NULL)
	{
		return NULL;
	}
	memset(ntf, 0, size);
	ntf->mask	= mask;

	for (i = 0; path[i]; ++i)
	{
		updateMaxName(ntf, path[i]);
	}

	unsigned long max_queued_events = 0;
	FILE* f = fopen(PATH_MAX_QUEUED_EVENTS, "r");
	if (f == NULL)
	{
		free(ntf);
		return NULL;
	}

	if (1 != fscanf(f, "%10lu", &max_queued_events))
	{
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
		free(ntf);
		return NULL;
	}

	for (i = 0; path[i]; ++i)
	{
		if (!access(path[i], F_OK))
		{
			if (-1 == addNotify(ntf, path[i], 0))
			{
				if (close(ntf->fd))
				{
					errno = 0;
				}
				free(ntf);
				return NULL;
			}
		}
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
			char* p = (char*)malloc(snprintf(NULL, 0, "%s%s", newpath, ntf->w[i] + strlen(oldpath)) + 1);
			if (p == NULL)
			{
				return -1;
			}
			sprintf(p, "%s%s", newpath, ntf->w[i] + strlen(oldpath));
			free(ntf->w[i]);
			ntf->w[i] = p;
		}
	}
	
	return 0;
}

static ssize_t totalRead(int fd, char** buf, size_t len)
{
	ssize_t total	= 0;
	ssize_t done	= 0;

	while (len > total)
	{
		done = read(fd, *buf + total, len - total);
		if (done > 0)
		{
			total += (size_t)done;
		}
		else if (0 == done)
		{
			break;
		}
		else
		{
			if (errno != EINTR && errno != EAGAIN)
			{
				total = -1;
				break;
			}
		}
	}

	return total;
}

static int checkFd(int fd)
{
	fd_set set;
	FD_ZERO(&set);
	FD_SET(fd, &set);
	struct timeval t = { 0, 0 };
	return select(fd + 1, &set, NULL, NULL, &t);
}

static int Select(int fd, int timeout)
{
	fd_set set;
	FD_ZERO(&set);
	FD_SET(fd, &set);
	struct timeval t = { 0, timeout };
	return select(fd + 1, &set, NULL, NULL, (timeout > 0) ? &t : NULL);
}

int waitNotify(Notify* ntf, char** const path, uint32_t* mask, int timeout, uint32_t* cookie)
{
	if (ntf == NULL
		|| path	== NULL)
	{
		errno = EINVAL;
		return -1;
	}

	int rd = 0;
	struct inotify_event* e = NULL;
	while ( 0 < (rd = checkFd(ntf->fd)) || NULL == (e = pullChainEvent(ntf)))
	{	
		if (!rd)
		{
			rd = Select(ntf->fd, timeout);
			if (!rd)
			{
				return timeout;
			}
		}

		if (-1 == rd)
		{
			return -1;
		}

		size_t length = 0;
		if (-1 == ioctl(ntf->fd, FIONREAD, &length))
		{
			return -1;
		}

		int event_size = sizeof(struct inotify_event);
		if (length > (ntf->max_queued_events * (event_size + ntf->max_name + 1)))
		{
			errno = EMSGSIZE;
			return -1;
		}
		
		char* buffer = (char*)malloc(length);
		if (buffer == NULL)
		{
			return -1;
		}
		memset(buffer, 0, length);

		ssize_t total_read = totalRead(ntf->fd, &buffer, length);
		if (length != total_read)
		{
			int rval = -1;
			if (errno == EINVAL)
			{
				*mask |= IN_Q_OVERFLOW;
				rval = 0;
			}

			free(buffer);	
			return rval;
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

	*path = malloc(1);
	if (*path == NULL)
	{
		return -1;
	}
	(*path)[0]	= '\0';

	if (e->wd > 0)
	{
		char* path_watch = ntf->w[e->wd - 1];
		if (path_watch)
		{
			free(*path);
			if (e->len)
			{
				*path = (char*)malloc(strlen(path_watch) + strlen(e->name) + 2);
				if (*path == NULL)
				{
					return -1;
				}
				sprintf(*path, "%s/%s", path_watch, e->name);
			}
			else
			{
				*path = (char*)malloc(strlen(path_watch) + 1);
				if (*path == NULL)
				{
					return -1;
				}
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
			char* oldpath = (char*)malloc(snprintf(NULL, 0, "%s/%s", C->path, C->name) + 1);
			if (oldpath == NULL)
			{
				freeCookie(C);
				free(e);
				return -1;
			}
			sprintf(oldpath, "%s/%s", C->path, C->name);

			char* newpath = (char*)malloc(snprintf(NULL, 0, "%s/%s", ntf->w[e->wd - 1], e->name) + 1);
			if (newpath == NULL)
			{
				free(oldpath);
				freeCookie(C);
				free(e);
				return -1;
			}
			sprintf(newpath, "%s/%s", ntf->w[e->wd - 1], e->name);

			if (-1 == renameWatches(ntf, oldpath, newpath))
			{
				free(oldpath);
				free(newpath);
				freeCookie(C);
				free(e);
				return -1;
			}
			
			/* DO NOT REMOVE - this is absolutely necessary */
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
		else
		{
			if (-1 == addNotify(ntf, *path, 0))
			{
				free(e);
				return -1;
			}		
		}
	}

	if (e->mask & IN_IGNORED)
	{
		free(ntf->w[e->wd - 1]);
		ntf->w[e->wd - 1] = NULL;
	}

	if (e->mask & IN_Q_OVERFLOW)
	{
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
	while (NULL != (e = pullChainEvent(ntf)))
	{
		free(e);
	}
	
	unsigned long i = 0;
	for (i = 0; i < ntf->size_w; i++)
	{
		if (ntf->w[i] != NULL)
		{
			free(ntf->w[i]);
		}
		inotify_rm_watch(ntf->fd, i + 1);
	}
	
	close(ntf->fd);

	free(ntf->w);
	free(ntf);	
	errno = safe_errno;
	
	return;
}

