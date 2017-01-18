#include <stdio.h>
#include <sys/types.h> 
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <stdlib.h>

#include "log.h"
#include "liblst.h"
#include "rnotify.h"

typedef struct {
	int heartbeat;
	int max_proc;
	char* exclude;
	int skip_zero_file;
	int skip_in_modify;
} options_t;

typedef struct _es {
	char* path;
	off_t size;
	struct timespec mtime;
	struct _es* n;
} echo_suppressor_t;

typedef struct _que {
	char* path;
	struct timespec mtime;
	struct _que* n;
} pending_que_t;

static int	queAdd(pending_que_t** q, const char* path);
static char*	quePickExpired(pending_que_t** q, int heartbeat);
static void	queDel(pending_que_t** q, const char* path);

static int	getEchoSuppressor(echo_suppressor_t** rs, const char* path);
static void	setEchoSuppressor(echo_suppressor_t** rs, const char* path);
static void	cutEchoSuppressor(echo_suppressor_t** rs, const char* path);
static void	freeEchoSuppressor(echo_suppressor_t* p);

static int	Excript(const char* path, options_t* opts);
static void	Sigactions(void);
static void	initDaemon(const char* pid_file);
static void	stopDaemon(const char* pid_file);
static void	printUsage(const char* name);
static void	Observer(char** path, options_t* opts);

static volatile sig_atomic_t	g_SIGTERM	= 0;
static volatile sig_atomic_t	g_SIGINT	= 0;
static volatile sig_atomic_t	g_SIGCHLD	= 0;
static volatile sig_atomic_t	g_VERBOSE	= 0;
static volatile sig_atomic_t	g_NODEMON	= 0;

static void* Malloc(size_t size)
{
	void *new_mem = (void*)malloc(size);
	if (new_mem == NULL)
	{
		exit(EXIT_FAILURE);
	}
	
	return new_mem;
}

static char* Strdup(const char* str)
{
	void* new_str = (str == NULL) ? strdup("") : strdup(str);
	if (new_str == NULL)
	{
		exit(EXIT_FAILURE);
	}
	
	return new_str;
}

static void sig_term_handler(int sig)
{
	(void)sig;
	g_SIGTERM = 1;
}

static void sig_chld_handler(int sig)
{
	(void)sig;
	g_SIGCHLD = 1;
}

int main(int argc, char** argv)
{
	g_VERBOSE	= 0;
	char* pid_file	= String("/var/run/%s.pid", argv[0]);
	char** path	= NULL;

	options_t options;
	
	char* opts	= "w:v:p:dh";
	int opt		= 0;

	while (-1 != (opt = getopt(argc, argv, opts)))
	{
		switch (opt)
		{
			case 'h':
				printUsage(argv[0]);
				break;
			case 'd':
				g_NODEMON = 1;
				break;
			case 'v':
				g_VERBOSE = atoi(optarg);
				break;
			case 'p':
				free(pid_file);
				pid_file = String("%s/%s", optarg, argv[0]);
				break;
			case 'w':
				lstPush(&path, optarg);
				break;
			default:
				break;
		}
	}

	if (!path || !path[0])
	{
		const char* message = "At least one path should be specifyed, exit";
		SYSLOG(ERROR, "%s", message);
		printf("%s\n", message);
		free(pid_file);
		return 0;
	}

	Sigactions();

	if (!g_NODEMON)
	{
		initDaemon(pid_file);
	}	

	Observer(path, &options);

	stopDaemon(pid_file);	
	
	lstFree(path);

	free(pid_file);
	
	return 0;
}

static int Excript(const char* path, options_t* opts)
{
	if (path == NULL)
	{
		return -1;
	}

	int fd1[2] = {0,};
	int fd2[2] = {0,};
	
	if (pipe(fd1))
	{
		return -1;
	}
	
	if (pipe(fd2))
	{
		return -1;
	}
	
	pid_t pid = fork();
	if (-1 == pid)
	{
		return -1;	
	}
	
	if (!pid)
	{
		close(fd1[0]);
		close(fd2[0]);
		dup2(fd1[1], STDOUT_FILENO);
		dup2(fd2[1], STDERR_FILENO);

		char* input	 = String("--input=%s", path);
		
		if (g_NODEMON && g_VERBOSE == 2)
		{
			printf("fhfstools --containerize %s\n", input);
		}
		execlp("fhfstools", "fhfstools", "--containerize", input, NULL);
		
		free(input);

		exit(errno);
	}
	else
	{
		close(fd1[1]);
		close(fd2[1]);

		int r		= 0;
		int total	= 0;
		char buf[512]	= {'\0',};

		char* respond = NULL;
		int safe_errno  = errno;
		while ((r = read(fd2[0], buf, sizeof(buf))))
		{
			if (-1 == r)
			{
				if (errno == EINTR
					|| errno == EAGAIN
					|| errno == EWOULDBLOCK)
				{
					errno = safe_errno;
					continue;
				}
				free(respond);
				return -1;
			}
			char* t = (char*)realloc(respond, total + r + 1);
			if (!t)
			{
				free(respond);
				return -1;
			}
			respond = t;
			memcpy(respond + total, buf, r);
			total += r;
		}

		if (respond)
		{
			*(respond + total) = '\0';
			SYSLOG(ERROR, "Child's process report (stderr): %s", respond);
			free(respond);
			respond = NULL;
		}
		
		r	= 0;
		total	= 0;
		buf[0]	= '\0';
		safe_errno  = errno;
		while ((r = read(fd1[0], buf, sizeof(buf))))
		{
			if (-1 == r)
			{
				if (errno == EINTR
					|| errno == EAGAIN
					|| errno == EWOULDBLOCK)
				{
					errno = safe_errno;
					continue;
				}
				free(respond);
				return -1;
			}
			char* t = (char*)realloc(respond, total + r + 1);
			if (!t)
			{
				free(respond);
				return -1;
			}
			respond = t;
			memcpy(respond + total, buf, r);
			total += r;
		}

		if (respond)
		{
			*(respond + total) = '\0';
			SYSLOG(INFO, "Child's process report (stdout): %s", respond);
		}	
		
		int status = 0;
		if (-1 == waitpid(pid, &status, 0))
		{
			SYSLOG(ERROR, "waitpid(%d)", pid);
			return -1;	
		}
		
		if (!WIFEXITED(status))
		{
			if (WIFSIGNALED(status))
			{
				#ifdef WCOREDUMP
					SYSLOG(DEBUG, "Child process (fhfstools) pid: %d terminated by signal: %d%s", 
					pid, WTERMSIG(status), WCOREDUMP(status) ? ", Core dumped" : "");				
				#else
					SYSLOG(DEBUG, "Child process (fhfstools) pid: %d terminated by signal: %d", pid, WTERMSIG(status));
				#endif
			}
			else
			{
				SYSLOG(DEBUG, "Child process (fhfstools) pid: %d abnormally terminated", pid);
			}

			return -1;	
		}
		else
		{
			SYSLOG(INFO, "Child process (fhfstools) pid: %d normally terminated with status: %d", pid, WEXITSTATUS(status));
		}
	}

	return 0;
}

static void initDaemon(const char* pid_file)
{
	pid_t pid   = 0;
	if (fork())
	{
		exit(EXIT_SUCCESS);
	}
	setsid();
	signal(SIGHUP, SIG_IGN);

	pid = fork();
	if (pid)
	{
		FILE* fp = fopen(pid_file, "w");
		if (!fp)
		{
			SYSLOG(ERROR, "fopen(%s) strerror=%s", pid_file, strerror(errno));
			exit(EXIT_FAILURE);
		}
		fprintf(fp, "%d\n", pid);
		fclose(fp);
		exit(EXIT_SUCCESS);
	}

	int i;
	chdir("/");
	umask(0);
	for (i = 0; i < 64; i++)
	{
		close(i);
	}
}

static void Sigactions(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_flags	= 0;
	sa.sa_handler	= sig_term_handler;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGTERM, &sa, NULL) == -1)
	{
		SYSLOG(ERROR, "sigaction(SIGTERM)");
		exit(EXIT_FAILURE);
	}

	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_flags	= 0;
	sa.sa_handler	= sig_term_handler;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGINT, &sa, NULL) == -1)
	{
		SYSLOG(ERROR, "sigaction(SIGINT)");
		exit(EXIT_FAILURE);
	}

	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_flags = 0;
	sa.sa_handler = sig_chld_handler;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGCHLD, &sa, NULL) == -1)
	{
		SYSLOG(ERROR, "sigaction(SIGCHLD)");
		exit(EXIT_FAILURE);
	}
}

static void stopDaemon(const char* pid_file)
{
	if (!access(pid_file, F_OK))
	{
		unlink(pid_file);
	}
}

static void Observer(char** path, options_t* opts)
{
	uint32_t init_mask = IN_MODIFY | IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF | IN_CLOSE_NOWRITE/*| IN_ATTRIB*/;

	Notify* ntf = initNotify(path, init_mask);
	if (ntf == NULL)
	{
		SYSLOG(ERROR, "initNotify()");
		exit(EXIT_FAILURE);
	}
	
	echo_suppressor_t* rs = NULL;
	pending_que_t* que = NULL;

	for (;;)
	{
		if (g_SIGTERM || g_SIGINT)
		{
			break;
		}
		
		char* np	= NULL;
		uint32_t mask	= 0;
		uint32_t cookie	= 0;
		int r = waitNotify(ntf, &np, &mask, opts->heartbeat, &cookie);
		if (r < 0)
		{
			if (errno == EINTR)
			{
				errno = 0;
				continue;
			}
			SYSLOG(ERROR, "Error in waitNotify(%s)", np);
			break;
		}
		
		if (mask & IN_CLOSE_WRITE)
		{
			SYSLOG(DEBUG, "IN_CLOSE_WRITE=%s", np);
			if (getEchoSuppressor(&rs, np))
			{
				struct stat st;
				memset(&st, 0, sizeof(st));
				stat(np, &st);
				SYSLOG(DEBUG, "file: %s size=%ld, skip=%d", np, st.st_size, opts->skip_zero_file);

				if (!opts->skip_zero_file || st.st_size)
				{
					if (Excript(np, opts))
					{
						SYSLOG(DEBUG, "Excript(%s)", np);
					}
					setEchoSuppressor(&rs, np);				
					queDel(&que, np);
				}
				else
				{
					SYSLOG(DEBUG, "Skip zero file: %s", np);
				}
			}
			else
			{
				SYSLOG(DEBUG, "Recursion detected (skipped): %s", np);
			}
		}
				
		if (mask & IN_CLOSE_NOWRITE)
		{
			//SYSLOG(DEBUG, "IN_CLOSE_NOWRITE=%s", np);
		}
				
		if (mask & IN_MODIFY)
		{
			if (!opts->skip_in_modify)
			{
				SYSLOG(DEBUG, "IN_MODIFY=%s", np);
				SYSLOG(DEBUG, "Add in pending queue: %s", np);
				queAdd(&que, np);
			}
		}
		
		if (mask & IN_CREATE)
		{
			SYSLOG(DEBUG, "IN_CREATE=%s", np);
		}
		
		if (mask & IN_DELETE)
		{
			SYSLOG(DEBUG, "IN_DELETE=%s", np);
			cutEchoSuppressor(&rs, np);
			queDel(&que, np);
		}
		
		if (mask & IN_MOVED_FROM)
		{
			SYSLOG(DEBUG, "IN_MOVED_FROM=%s cookie=%d", np, cookie);
			cutEchoSuppressor(&rs, np);
			queDel(&que, np);
		}
		
		if (mask & IN_MOVED_TO)
		{
			SYSLOG(DEBUG, "IN_MOVED_TO=%s cookie=%d", np, cookie);
			struct stat st;
			memset(&st, 0, sizeof(st));
			stat(np, &st);
			SYSLOG(DEBUG, "file: %s size=%ld, skip=%d", np, st.st_size, opts->skip_zero_file);

			if (!opts->skip_zero_file || st.st_size)
			{
				if (Excript(np, opts))
				{
					SYSLOG(ERROR, "Excript(%s)", np);
				}
				setEchoSuppressor(&rs, np);				
				queDel(&que, np);
			}
			else
			{
				SYSLOG(DEBUG, "Skip zero file: %s", np);
			}
		}
		
		if (mask & IN_DELETE_SELF)
		{
			//SYSLOG(DEBUG, "IN_DELETE_SELF=%s", np);
		}
		
		if (mask & IN_MOVE_SELF)
		{
			//SYSLOG(DEBUG, "IN_MOVE_SELF=%s", np);
		}

		free(np);
		
		while ((np = quePickExpired(&que, opts->heartbeat)))
		{
			SYSLOG(DEBUG, "Proccessing file from pending queue: %s", np);
			struct stat st;
			memset(&st, 0, sizeof(st));
			stat(np, &st);
			SYSLOG(DEBUG, "file: %s size=%ld, skip=%d", np, st.st_size, opts->skip_zero_file);

			if (!opts->skip_zero_file || st.st_size)
			{
				if (Excript(np, opts))
				{
					SYSLOG(ERROR, "Excript(%s)", np);
				}
				setEchoSuppressor(&rs, np);				
				queDel(&que, np);
			}
			else
			{
				SYSLOG(DEBUG, "Skip zero file: %s", np);
			}
		}
	}
	freeEchoSuppressor(rs);
	freeNotify(ntf);
}

static int getEchoSuppressor(echo_suppressor_t** rs, const char* path)
{
	if (rs == NULL || *rs == NULL || path == NULL)
	{
		return -1;
	}

	struct stat st;
	memset(&st, 0, sizeof(st));
	stat(path, &st);

	SYSLOG(DEBUG, "file: %s size=%ld", path, st.st_size);
	echo_suppressor_t* p = *rs;
	while (p)
	{	
		if (p->path && !strcmp(p->path, path))
		{
			if (p->mtime.tv_sec == st.st_mtim.tv_sec && p->mtime.tv_nsec == st.st_mtim.tv_nsec && p->size == st.st_size)
			{
				return 0;
			}
			p->mtime.tv_sec		= st.st_mtim.tv_sec;
			p->mtime.tv_nsec	= st.st_mtim.tv_nsec;
			p->size			= st.st_size;
			
			return -1;
		}
		p = p->n;
	}

	return -1;
}

static void setEchoSuppressor(echo_suppressor_t** rs, const char* path)
{
	if (rs == NULL || path == NULL)
	{
		return;
	}

	struct stat st;
	memset(&st, 0, sizeof(st));
	stat(path, &st);

	SYSLOG(DEBUG, "file: %s size=%ld", path, st.st_size);
	echo_suppressor_t* p = *rs;
	while (p)
	{
		if (p->path && !strcmp(p->path, path))
		{
			p->mtime.tv_sec		= st.st_mtim.tv_sec;
			p->mtime.tv_nsec	= st.st_mtim.tv_nsec;
			p->size			= st.st_size;
			
			return;
		}
		
		if (p->n == NULL)
		{
			break;
		}
		
		p = p->n;
	}

	echo_suppressor_t* new_p = Malloc(sizeof(echo_suppressor_t));		
	new_p->path		= Strdup(path);
	new_p->size		= st.st_size;
	new_p->mtime.tv_sec	= st.st_mtim.tv_sec;
	new_p->mtime.tv_nsec	= st.st_mtim.tv_nsec;
	new_p->n		= NULL;
	
	if (p == NULL)
	{
		*rs = new_p;
		return;
	}

	p->n = new_p;

	return;	
}

static void freeEchoSuppressor(echo_suppressor_t* p)
{
	while (p)
	{
		free(p->path);
		echo_suppressor_t* p_old = p;
		p = p->n;
		free(p_old);
	}
}

static void cutEchoSuppressor(echo_suppressor_t** rs, const char* path)
{
	if (rs == NULL || *rs == NULL || path == NULL)
	{
		return;
	}
	
	if ((*rs)->path && !strcmp((*rs)->path, path))
	{
		echo_suppressor_t* t = *rs;
		*rs = (*rs)->n;
		free(t->path);
		free(t);		
		
		return;
	}

	echo_suppressor_t* p = *rs;
	while (p->n)
	{	
		if (p->n->path && !strcmp(p->n->path, path))
		{
			echo_suppressor_t* t = p->n;
			p->n = p->n->n;
			free(t->path);
			free(t);
			
			return;
		}
		p = p->n;
	}

	return;
}

static int queAdd(pending_que_t** q, const char* path)
{
	if (q == NULL || path == NULL)
	{
		return -1;
	}
	
	struct stat st;
	memset(&st, 0, sizeof(st));
	stat(path, &st);
	
	pending_que_t* p = *q;
	while (p)
	{
		if (p->path && !strcmp(p->path, path))
		{
			p->mtime.tv_sec   = st.st_mtim.tv_sec;
			p->mtime.tv_nsec  = st.st_mtim.tv_nsec;

			return 0;
		}
		
		if (p->n == NULL)
		{
			break;
		}
		
		p = p->n;
	}
	
	pending_que_t* new_p	= Malloc(sizeof(pending_que_t));		
	new_p->path		= Strdup(path);
	new_p->mtime.tv_sec	= st.st_mtim.tv_sec;
	new_p->mtime.tv_nsec	= st.st_mtim.tv_nsec;
	new_p->n		= NULL;

	if (p == NULL)
	{
		*q = new_p;
		return 0;
	}

	p->n = new_p;

	return 0;
}

static char* quePickExpired(pending_que_t** q, int heartbeat)
{
	if (q == NULL || *q == NULL)
	{
		return NULL;
	}

	struct timespec t = {0,0};
	if (clock_gettime(CLOCK_REALTIME, &t))
	{
		return (*q)->path;
	}
		
	char* rval = NULL;
	
	pending_que_t* p = *q;
	while (p)
	{
		struct stat st;
		memset(&st, 0, sizeof(st));
		stat(p->path, &st);
		
		long s = t.tv_sec - st.st_mtim.tv_sec;
		long n = st.st_mtim.tv_nsec ? 1E9L - st.st_mtim.tv_nsec + t.tv_nsec : t.tv_nsec;
		long delta = s * 1E6L + n / 1E3L;
		if (delta >= heartbeat)
		{
			rval = p->path;
			break;
		}
		p = p->n;
	}
	
	return rval;
}

static void queDel(pending_que_t** q, const char* path)
{	
	if (q == NULL || *q == NULL || path == NULL)
	{
		return;
	}
	
	pending_que_t* p = *q;
	if (p->path && !strcmp(p->path, path))
	{
		*q = p->n;
		free(p->path);
		free(p);
		
		return;
	}

	while (p->n)
	{
		if (p->n->path && !strcmp(p->n->path, path))
		{
			pending_que_t* t = p->n;
			p->n = p->n->n;
			free(t->path);
			free(t);
			
			return;
		}
		p = p->n;
	}
}

static void printUsage(const char* name)
{
	printf("\nversion: " "2.0.0" "\n\nUsage: %s [-options] [-w path1 -w path2 ...] \n\nOptions:\n"
		"\t-v [verbose level 0-2] default: 0 (only errors)\n"
		"\t-p [path to pid file] default: /var/run\n"
		"\t-d no daemon mode\n"
		"\t-h this message\n \n", name);
	exit(EXIT_SUCCESS);
}
