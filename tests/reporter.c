/*
 * Test reporter for librnotify stress tests.
 *
 * Watches one directory (argv[1]) and prints one line per event:
 *
 *     EVENT <FLAGS> <cookie> <path>
 *
 * Flags are emitted as a "|"-joined list of symbolic names (CREATE,
 * MOVED_FROM, ISDIR, IN_Q_OVERFLOW, ...) so the shell wrapper can
 * grep without hex-decoding. Stdout is line-buffered so events show
 * up immediately on the consuming side.
 *
 * "READY" is written to stderr after initNotify succeeds; tests can
 * poll the stderr file for that token before starting their workload.
 *
 * Exits 0 on SIGTERM (clean shutdown by the test), nonzero on error.
 */

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rnotify.h"

static volatile sig_atomic_t g_stop = 0;
static void on_term(int sig) { (void)sig; g_stop = 1; }

struct flag { uint32_t bit; const char* name; };
static const struct flag g_flags[] = {
    { IN_ACCESS,        "ACCESS" },
    { IN_MODIFY,        "MODIFY" },
    { IN_ATTRIB,        "ATTRIB" },
    { IN_CLOSE_WRITE,   "CLOSE_WRITE" },
    { IN_CLOSE_NOWRITE, "CLOSE_NOWRITE" },
    { IN_OPEN,          "OPEN" },
    { IN_MOVED_FROM,    "MOVED_FROM" },
    { IN_MOVED_TO,      "MOVED_TO" },
    { IN_CREATE,        "CREATE" },
    { IN_DELETE,        "DELETE" },
    { IN_DELETE_SELF,   "DELETE_SELF" },
    { IN_MOVE_SELF,     "MOVE_SELF" },
    { IN_ISDIR,         "ISDIR" },
    { IN_IGNORED,       "IGNORED" },
    { IN_Q_OVERFLOW,    "OVERFLOW" },
};

static void print_mask(uint32_t mask)
{
    int first = 1;
    for (size_t i = 0; i < sizeof(g_flags)/sizeof(g_flags[0]); i++)
    {
        if (mask & g_flags[i].bit)
        {
            if (!first) fputc('|', stdout);
            fputs(g_flags[i].name, stdout);
            first = 0;
        }
    }
    if (first)
    {
        printf("0x%08x", mask);
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <dir>\n", argv[0]);
        return 2;
    }

    /* line-buffered so each EVENT flushes immediately */
    setvbuf(stdout, NULL, _IOLBF, 0);

    struct sigaction sa = { .sa_handler = on_term };
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);

    Notify* ntf = initNotify(argv[1], IN_ALL_EVENTS, NULL);
    if (ntf == NULL)
    {
        fprintf(stderr, "initNotify(%s) failed: %s\n", argv[1], strerror(errno));
        return 1;
    }

    fprintf(stderr, "READY\n");
    fflush(stderr);

    int exitcode = 0;
    while (!g_stop)
    {
        char*    path   = NULL;
        uint32_t mask   = 0;
        uint32_t cookie = 0;
        int rc = waitNotify(ntf, &path, &mask, 200, &cookie);
        if (rc == 0)
        {
            fputs("EVENT ", stdout);
            print_mask(mask);
            printf(" %u %s\n", cookie, path ? path : "");
            free(path);
        }
        else if (rc == -1)
        {
            if (errno == EINTR) continue;
            fprintf(stderr, "waitNotify error: %s\n", strerror(errno));
            free(path);
            exitcode = 1;
            break;
        }
        /* rc > 0: timeout, loop and check g_stop */
    }

    freeNotify(ntf);
    return exitcode;
}
