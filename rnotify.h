#ifndef __RNOTIFY_H
#define __RNOTIFY_H

#include <sys/inotify.h>
#include <regex.h>

Notify*	initNotify(const char* path, const uint32_t mask, const char* exclude);
int	waitNotify(Notify* ntf, char** const path, uint32_t* mask, const int timeout, uint32_t* cookie);
void	freeNotify(Notify* ntf);

#endif // __RNOTIFY_H
