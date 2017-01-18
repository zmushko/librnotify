/*
 *  liblst.h
 *
 *  framework library
 *
 *  Author: Andrey Zmushko
 */

#ifndef __LIBLST_H
#define __LIBLST_H

// #define _GNU_SOURCE // 

#include <stdlib.h> // size_t definition

#ifdef __cplusplus

extern "C" {

#endif

ssize_t lstPush(char*** lst, const char* str);
void	lstFree(char** lst);
char**	lstReadDir(const char* path);
char*	String(const char* format, ...);

#ifdef __cplusplus

}

#endif

#endif
