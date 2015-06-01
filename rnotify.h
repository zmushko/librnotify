#ifndef __RNOTIFY_H
#define __RNOTIFY_H

#include <sys/inotify.h>
#include <regex.h>

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


Notify*	initNotify(const char* path, const uint32_t mask, const char* exclude);
int	waitNotify(Notify* ntf, char** const path, uint32_t* mask, const int timeout, uint32_t* cookie);
void	freeNotify(Notify* ntf);

#endif // __RNOTIFY_H
