/**
  * https://github.com/zmushko/librnotify
  * use it as you want but keep this header (if you want)
  */
#ifndef __RNOTIFY_H
#define __RNOTIFY_H

#include <stdint.h>
#include <sys/inotify.h>

typedef struct _rnotify Notify;

#ifdef __cplusplus
extern "C" {
#endif
	Notify*	initNotify(char** path, const uint32_t mask);
	int	waitNotify(Notify* ntf, char** const path, uint32_t* mask, const int timeout, uint32_t* cookie);
	void	freeNotify(Notify* ntf);
#ifdef __cplusplus
   }
#endif

#endif // __RNOTIFY_H
