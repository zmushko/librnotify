/*
 *  liblst.h
 *
 *  framework library
 *
 *  Author: Andrey Zmushko
 */

#ifndef LIBRNOTIFY_LIBLST_H_
#define LIBRNOTIFY_LIBLST_H_

// #define _GNU_SOURCE // 

#include <stdlib.h> // size_t definition

#ifdef __cplusplus

extern "C" {

#endif

ssize_t lstPush(char*** lst, const char* str);
void lstFree(char** lst);
char** lstReadDir(const char* path);
char* lstString(const char* format, ...);

#ifdef __cplusplus

}

#endif

#endif
