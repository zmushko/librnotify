#ifndef RNOTIFY_INTERNAL_H
#define RNOTIFY_INTERNAL_H

#include <sys/inotify.h>
#include <regex.h>

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

struct _notify {
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
};

#endif // RNOTIFY_INTERNAL_H

