/*
 *  log.h
 *
 *  Author: Andrey Zmushko (c) 2016
 */

#ifndef __LOG_H
#define __LOG_H

#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <syslog.h>

enum 
{
	ERROR	= LOG_ERR, 
	DEBUG	= LOG_DEBUG, 
	INFO	= LOG_INFO
};

#define SYSLOG(A, ...)	do{ \
				if (g_VERBOSE < 2 && A == LOG_DEBUG) break; \
				if (g_VERBOSE < 1 && (A == LOG_DEBUG || A == LOG_INFO)) break; \
				int safe_errno = errno; \
				char error_buf[256] = {'\0', }; \
				strerror_r(safe_errno, error_buf, 256); \
				char* args = malloc(snprintf(NULL, 0, ## __VA_ARGS__) + 1); \
				if (args == NULL) \
				{ \
					printf("ABORT: can not allocate memory\n"); \
					exit(EXIT_FAILURE); \
				} \
				sprintf(args, ## __VA_ARGS__); \
				char* message = malloc(snprintf(NULL, 0, "%s pid:%d file:%s function:%s line:%d %s: %s'", #A, getpid(), __FILE__, __FUNCTION__, __LINE__, args, error_buf) + 1); \
				if (message == NULL) \
				{ \
					free(args); \
					printf("ABORT: can not allocate memory\n"); \
					exit(EXIT_FAILURE); \
				} \
				sprintf(message, "%s pid:%d file:%s function:%s line:%d %s%s%s", #A, getpid(), __FILE__, __FUNCTION__, __LINE__, args, (safe_errno && A == LOG_ERR) ? ": " : "", (safe_errno && A == LOG_ERR) ? error_buf : ""); \
				syslog(LOG_ERR, "%s", message); \
				if (g_NODEMON) \
				{ \
					printf("%s\n", message); \
				} \
				free(message); \
				free(args); \
				errno = 0; \
			}while(0)

#endif
