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
#include <regex.h>
#include <limits.h>

#include "liblst.h"
#include "rnotify.h"

/*
 * FIFO node holding one already-decoded inotify_event. The queue is
 * singly linked: ntf->head is the next event to be pulled (oldest),
 * ntf->tail is where new events are appended (newest), and `next`
 * always points further from head toward tail.
 */
struct chainEvent
{
    struct inotify_event* e;
    struct chainEvent* next;
};

/*
 * One pending IN_MOVED_FROM. Kept in a doubly-linked list so we can
 * delete by wd (dropCookiesForWd) without re-walking from head. The
 * matching IN_MOVED_TO is located by `cookie` value.
 */
struct Cookie
{
    struct Cookie* prev;
    uint32_t cookie;
    int wd;            /* watch descriptor that emitted IN_MOVED_FROM */
    char* path;        /* directory containing the moved entry */
    char* name;        /* the entry's name within that directory */
    struct Cookie* next;
};

/*
 * Internal Notify state. Opaque to consumers — they hold it through
 * the public Notify typedef.
 *   fd                : inotify fd from inotify_init().
 *   w / size_w        : sparse path table indexed by wd-1. size_w is
 *                       capacity; entries are NULL for unused slots.
 *                       See watchPath() and addNotify().
 *   max_name          : pathconf(_PC_NAME_MAX), updated on watch add.
 *   max_queued_events : snapshot of /proc/sys/fs/inotify/max_queued_events
 *                       read at init time; used to sanity-check FIONREAD.
 *   exclude           : optional compiled regex; entries whose name
 *                       matches are filtered out of the chain.
 *   mask              : event mask to install on every watch.
 *   head / tail       : FIFO queue of decoded inotify_event copies.
 *   cookies           : pending IN_MOVED_FROM entries awaiting their TO.
 */
struct _rnotify
{
    int fd;
    unsigned int size_w;
    char** w;
    long max_name;
    unsigned long max_queued_events;
    regex_t* exclude;
    uint32_t mask;
    struct chainEvent* head;
    struct chainEvent* tail;
    struct Cookie* cookies;
};

#define PATH_MAX_QUEUED_EVENTS "/proc/sys/fs/inotify/max_queued_events"

/*
 * Prepend a new pending cookie. See addCookie's body for the head
 * insertion rationale.
 *
 * Returns 0 on success, -1 on allocation failure (errno set).
 */
static int addCookie(struct Cookie** p, int wd, const char* path, const char* name, uint32_t cookie)
{
    struct Cookie* new_p = (struct Cookie*)malloc(sizeof(struct Cookie));
    if (new_p == NULL)
    {
        return -1;
    }
    memset(new_p, 0, sizeof(struct Cookie));
    new_p->wd = wd;

    new_p->path = lstString("%s", path);
    if (new_p->path == NULL)
    {
        free(new_p);
        return -1;
    }

    new_p->name = lstString("%s", name);
    if (new_p->name == NULL)
    {
        free(new_p->path);
        free(new_p);
        return -1;
    }

    new_p->cookie = cookie;

    /* Head insertion: typical workload pairs IN_MOVED_FROM with the
     * immediately-following IN_MOVED_TO, so most lookups hit the head
     * after one step. Orphan cookies (FROM with no matching TO — move
     * out of watched scope, or a buffer overflow) accumulate towards
     * the tail and live until freeNotify; on a stable watch set this
     * is a few entries at most, but a hostile workload could pile up
     * arbitrarily many. */
    new_p->prev = NULL;
    new_p->next = *p;
    if (*p != NULL)
    {
        (*p)->prev = new_p;
    }
    *p = new_p;

    return 0;
}

/*
 * Find and detach a pending cookie by its uint32 value. Returns the
 * detached Cookie (caller owns it and must freeCookie) or NULL when
 * nothing matches. The list scan starts at the head, which after head
 * insertion (addCookie) is usually exactly one step for FROM/TO pairs
 * that arrive consecutively.
 */
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

/* Release a Cookie detached from the list. */
static void freeCookie(struct Cookie* p)
{
    free(p->name);
    free(p->path);
    free(p);
}

/*
 * Drop every pending cookie that came from a given watch descriptor.
 * Called when IN_IGNORED retires that wd: any matching IN_MOVED_TO
 * is now impossible, and leaving the cookies around would let a
 * later wd-recycle collide with a stale entry (kernel reuses wd
 * numbers via IDR after inotify_rm_watch).
 */
static void dropCookiesForWd(struct Cookie** head, int wd)
{
    struct Cookie* c = *head;
    while (c != NULL)
    {
        struct Cookie* next = c->next;
        if (c->wd == wd)
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
            freeCookie(c);
        }
        c = next;
    }
}

/*
 * Append a deep-copy of `e` to the tail of the event FIFO. Returns 0
 * on success, 0 (silently) when the event's name matches the configured
 * exclude regex, and -1 on allocation failure (errno set).
 *
 * Ownership: a successful push transfers a freshly malloc'd copy of `e`
 * into the queue; the caller still owns and releases `e` itself
 * (freeChainEvent). The eventual pullChainEvent returns that internal
 * copy and the puller becomes responsible for freeChainEvent on it.
 */
static int pushChainEvent(Notify* ntf, struct inotify_event* e)
{
    if (ntf == NULL || e == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    if (ntf->exclude && !regexec(ntf->exclude, e->name, 0, NULL, 0))
    {
        return 0;
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
        freeChainEvent(event);
        return -1;
    }
    element->e = event;
    element->next = NULL;

    if (ntf->tail != NULL)
    {
        ntf->tail->next = element;
    }
    else
    {
        ntf->head = element;
    }
    ntf->tail = element;

    return 0;
}

/*
 * Release an inotify_event obtained from pullChainEvent (or constructed
 * locally for a pushChainEvent call). Defined as a single-step wrapper
 * around free() so the pull-event lifecycle is greppable and so the
 * single free site is easy to extend when the event ever grows
 * non-trivial owned data.
 */
static void freeChainEvent(struct inotify_event* event)
{
    free(event);
}

/*
 * Remove and return the oldest pending inotify_event, or NULL if the
 * queue is empty. The returned pointer is owned by the caller and
 * must be released with freeChainEvent() once consumed.
 */
static struct inotify_event* pullChainEvent(Notify* ntf)
{
    if (ntf->head == NULL)
    {
        return NULL;
    }

    struct chainEvent* element = ntf->head;
    struct inotify_event* event = element->e;
    ntf->head = element->next;
    if (ntf->head == NULL)
    {
        ntf->tail = NULL;
    }
    free(element);

    return event;
}

/*
 * Track the maximum NAME_MAX across all watched paths; the FIONREAD
 * size sanity check in waitNotify multiplies this by max_queued_events
 * to bound an acceptable buffer size.
 */
static void updateMaxName(Notify* ntf, char* path)
{
    if (!access(path, F_OK))
    {
        long max_name = pathconf(path, _PC_NAME_MAX);
        ntf->max_name = (max_name > ntf->max_name) ? max_name : ntf->max_name;
    }
}

/**
 * Install an inotify watch on `path` and synthesise IN_CREATE events
 * for entries already present in the directory (so the caller never
 * misses files that appeared between mkdir(2) and our watch).
 *
 * Returns:
 *    1 — watch installed.
 *    0 — path no longer exists (ENOENT from inotify_add_watch); a
 *        benign no-op so callers can ignore racing deletions.
 *   -1 — error (errno set).
 */
static int addNotify(Notify* ntf, const char* path, uint32_t cookie)
{
    errno = 0;
    /*
     * IN_DONT_FOLLOW is always set: if `path` is a symlink, watch the
     * link itself rather than dereferencing it. Without this flag an
     * attacker with write access to a watched directory could plant a
     * symlink and steer the recursive descent into an arbitrary path
     * (e.g. /etc). Coupled with the lstat() check on directory entries
     * elsewhere in this file, this is the no-follow guarantee.
     */
    int wd = inotify_add_watch(ntf->fd, path, ntf->mask | IN_DONT_FOLLOW);
    if (-1 == wd)
    {
        if (errno == ENOENT)
        {
            return 0;
        }
        return -1;
    }

    if (wd > (int)ntf->size_w)
    {
        unsigned int new_cap = ntf->size_w ? ntf->size_w : 16;
        while (new_cap < (unsigned int)wd)
        {
            if (new_cap > UINT_MAX / 2)
            {
                new_cap = (unsigned int)wd;
                break;
            }
            new_cap *= 2;
        }

        char** t = (char**)realloc(ntf->w, sizeof(void*) * new_cap);
        if (t == NULL)
        {
            /* realloc(3) leaves the original block intact on failure;
             * do not clobber ntf->w or freeNotify will dereference NULL.
             */
            return -1;
        }
        ntf->w = t;
        for (size_t i = ntf->size_w; i < new_cap; i++)
        {
            ntf->w[i] = NULL;
        }
        ntf->size_w = new_cap;
    }

    if (ntf->w[wd - 1] != NULL)
    {
        free(ntf->w[wd - 1]);
    }

    ntf->w[wd - 1] = lstString("%s", path);
    if (ntf->w[wd - 1] == NULL)
    {
        return -1;
    }

    char** elems = lstReadDir(path);
    if (elems == NULL)
    {
        /* watch is in place; we just couldn't enumerate existing
         * entries (path is a regular file, or permission denied) */
        return 1;
    }

    const size_t event_size = sizeof(struct inotify_event);
    size_t i = 0;
    while (elems[i])
    {
        char* path_elem = lstString("%s/%s", path, elems[i]);
        if (path_elem == NULL)
        {
            lstFree(elems);
            return -1;
        }

        struct stat sb;
        /* lstat — never dereference a symlink here. A symlink-to-dir
         * must not be reported as IN_ISDIR or the recursive descent
         * would follow it (IN_DONT_FOLLOW on inotify_add_watch already
         * refuses to install the watch, but emitting IN_ISDIR for a
         * symlink is still semantically wrong). */
        int is_dir = (!lstat(path_elem, &sb) && S_ISDIR(sb.st_mode)) ? 1 : 0;

        size_t name_size = strlen(elems[i]) + 1;
        struct inotify_event* e = (struct inotify_event*)malloc(event_size + name_size);
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
        e->len = name_size;
        e->cookie = cookie;
        memcpy(e->name, elems[i], name_size);
        if (-1 == pushChainEvent(ntf, e))
        {
            freeChainEvent(e);
            free(path_elem);
            lstFree(elems);
            return -1;
        }
        freeChainEvent(e);

        if (!is_dir)
        {
            e = (struct inotify_event*)malloc(event_size + name_size);
            if (e == NULL)
            {
                free(path_elem);
                lstFree(elems);
                return -1;
            }
            memset(e, 0, event_size);

            e->wd = wd;
            e->mask = IN_CLOSE_WRITE;
            e->len = name_size;
            memcpy(e->name, elems[i], name_size);

            if (-1 == pushChainEvent(ntf, e))
            {
                freeChainEvent(e);
                free(path_elem);
                lstFree(elems);
                return -1;
            }
            freeChainEvent(e);
        }
        free(path_elem);
        i++;
    }

    lstFree(elems);

    return 1;
}

/*
 * Public API.
 *
 * Initialise a Notify monitoring `path` (NULL-terminated array of
 * absolute paths). `mask` is the inotify event mask to install on
 * every watch; IN_DONT_FOLLOW is added unconditionally. `exclude`,
 * if non-NULL, is compiled as POSIX extended regex and any event
 * whose `name` field matches is dropped before it reaches the chain.
 *
 * Each path is descended recursively: existing entries are surfaced
 * as synthetic IN_CREATE events (and IN_CLOSE_WRITE for regular
 * files) so the caller never has to do an initial scan separately.
 *
 * Returns a Notify* on success. Returns NULL with errno set on
 * failure: EINVAL for a NULL/empty path array, ENOENT when none of
 * the supplied paths existed at install time, or any errno from
 * inotify_init/inotify_add_watch/malloc.
 */
Notify* initNotify(char** path, const uint32_t mask, const char* exclude)
{
    if (path == NULL || path[0] == NULL)
    {
        errno = EINVAL;
        return NULL;
    }

    size_t i = 0;
    size_t size = sizeof(Notify);
    Notify* ntf = (Notify*)malloc(size);
    if (ntf == NULL)
    {
        return NULL;
    }
    memset(ntf, 0, size);
    ntf->mask = mask;

    for (i = 0; path[i]; ++i)
    {
        updateMaxName(ntf, path[i]);
    }

    if (exclude)
    {
        regex_t* preg = (regex_t*)malloc(sizeof(regex_t));
        if (preg == NULL)
        {
            free(ntf);
            return NULL;
        }
        memset(preg, 0, sizeof(regex_t));
        if (0 != regcomp(preg, exclude, REG_EXTENDED))
        {
            free(ntf);
            free(preg);
            return NULL;
        }
        ntf->exclude = preg;
    }

    unsigned long max_queued_events = 0;
    FILE* f = fopen(PATH_MAX_QUEUED_EVENTS, "r");
    if (f == NULL)
    {
        if (ntf->exclude)
        {
            regfree(ntf->exclude);
        }
        free(ntf);
        return NULL;
    }

    if (1 != fscanf(f, "%10lu", &max_queued_events))
    {
        if (ntf->exclude)
        {
            regfree(ntf->exclude);
        }

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

    ntf->fd = inotify_init();
    if (-1 == ntf->fd)
    {
        if (ntf->exclude)
        {
            regfree(ntf->exclude);
        }

        free(ntf);
        return NULL;
    }

    int installed = 0;
    for (i = 0; path[i]; ++i)
    {
        int rc = addNotify(ntf, path[i], 0);
        if (rc == -1)
        {
            if (ntf->exclude)
            {
                regfree(ntf->exclude);
            }
            close(ntf->fd);
            free(ntf);
            return NULL;
        }
        if (rc > 0)
        {
            installed++;
        }
    }

    if (installed == 0)
    {
        if (ntf->exclude)
        {
            regfree(ntf->exclude);
        }
        close(ntf->fd);
        free(ntf);
        errno = ENOENT;
        return NULL;
    }

    return ntf;
}

/*
 * Rewrite every path in ntf->w[] that lives under `oldpath` so the
 * prefix becomes `newpath`. Called when a watched directory is moved
 * inside the watch set: the kernel keeps the wd, but our cached path
 * for that wd (and for any descendant watch) becomes stale and needs
 * to be patched up. Whole-prefix replacement, including a guard that
 * we only match `oldpath` itself or `oldpath/`-something — not a path
 * that merely begins with the same characters (e.g. /foo vs /foobar).
 *
 * Returns 0 on success, -1 on allocation failure (errno set).
 */
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
            char* p = lstString("%s%s", newpath, ntf->w[i] + strlen(oldpath));
            if (p == NULL)
            {
                return -1;
            }
            free(ntf->w[i]);
            ntf->w[i] = p;
        }
    }

    return 0;
}

/*
 * Read exactly `len` bytes from `fd` into `*buf`, looping over short
 * reads and retrying on EINTR/EAGAIN. Returns the total number read
 * (which may be < len at EOF) or -1 on a non-recoverable read error
 * (errno set). The `buf` is a pointer-to-pointer only to keep the
 * caller's existing variable mutable through the call; the function
 * does not realloc.
 */
static ssize_t totalRead(int fd, char** buf, size_t len)
{
    ssize_t total_read = 0;
    ssize_t done = 0;

    while ((ssize_t)len > total_read)
    {
        done = read(fd, *buf + total_read, len - total_read);
        if (done > 0)
        {
            total_read += (size_t)done;
        }
        else if (0 == done)
        {
            break;
        }
        else
        {
            if (errno != EINTR && errno != EAGAIN)
            {
                total_read = -1;
                break;
            }
        }
    }

    return total_read;
}

/*
 * Non-blocking probe: does `fd` have data ready right now?
 *
 *   > 0  data ready (select() reported fd readable)
 *   = 0  no data ready
 *   < 0  error (errno set; typically EBADF)
 *
 * Used by waitNotify to decide whether to skip select() and read
 * immediately, vs. fall through to the blocking wait.
 */
static int checkFd(int fd)
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    struct timeval t = { 0, 0 };
    return select(fd + 1, &set, NULL, NULL, &t);
}

/*
 * Wait for `fd` to become readable, up to `timeout` milliseconds.
 *
 *   timeout < 0   block indefinitely
 *   timeout = 0   non-blocking poll
 *   timeout > 0   wait at most that many milliseconds
 *
 *   > 0  fd is readable
 *   = 0  timeout expired with no activity
 *   < 0  error (errno set)
 */
static int Select(int fd, int timeout)
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    struct timeval t;
    t.tv_sec  = timeout / 1000;
    t.tv_usec = (timeout % 1000) * 1000;
    return select(fd + 1, &set, NULL, NULL, (timeout < 0) ? NULL : &t);
}

/*
 * Resolve a watch descriptor to its stored path, or NULL if the wd is
 * out of range or the slot has already been invalidated (e.g. by a
 * prior IN_IGNORED). IN_Q_OVERFLOW carries wd = -1 from the kernel,
 * which is the original reason this guard exists.
 */
static char* watchPath(const Notify* ntf, int wd)
{
    if (wd <= 0 || (unsigned int)wd > ntf->size_w)
    {
        return NULL;
    }
    return ntf->w[wd - 1];
}

/*
 * Public API.
 *
 * Wait for the next filesystem event on `ntf`. When an event is
 * delivered:
 *   *path   is set to a freshly malloc'd null-terminated absolute
 *           path (caller frees);
 *   *mask   is set to the inotify mask bits, or IN_Q_OVERFLOW if
 *           the kernel's queue was lost (synthetic);
 *   *cookie is set to the kernel-assigned cookie if non-NULL.
 *
 * `mask` and `cookie` may be NULL; `path` must not be.
 *
 * `timeout` follows the same -1/0/>0 convention as Select(): -1 is
 * block forever, 0 is non-blocking poll, >0 is milliseconds.
 *
 * Returns:
 *    0  on event delivered.
 *  >0   timeout: returns the original `timeout` value unchanged.
 *   -1  error (errno set). EINVAL for a NULL ntf/path; EPROTO for a
 *       truncated event; any errno from the underlying read/select.
 *
 * The internal recursive-watch bookkeeping (auto-adding watches on
 * IN_CREATE|IN_ISDIR, renaming on IN_MOVED_TO, retiring on
 * IN_IGNORED) happens before this call returns, transparent to the
 * caller.
 */
int waitNotify(Notify* ntf, char** const path, uint32_t* mask, int timeout, uint32_t* cookie)
{
    if (ntf == NULL
        || path == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    if (mask)
    {
        *mask = 0;
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

        if (length == 0)
        {
            /* Spurious wake-up: select() reported readable but the
             * queue is empty by the time we look. Avoid malloc(0)
             * (implementation-defined: may return NULL and be mistaken
             * for an allocation failure) and go back to waiting.
             */
            continue;
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
        if ((ssize_t)length != total_read)
        {
            int rval = -1;
            if (errno == EINVAL)
            {
                if (mask)
                {
                    *mask = IN_Q_OVERFLOW;
                }
                rval = 0;
            }

            free(buffer);
            return rval;
        }

        /*
         * Walk packed inotify events. Two invariants keep the
         * pointer-cast read of e->len safe:
         *  - malloc(3) returns memory aligned to alignof(max_align_t),
         *    which is >= alignof(struct inotify_event) (== 4) on every
         *    Linux ABI;
         *  - the kernel rounds e->len up so successive events stay
         *    aligned to alignof(struct inotify_event); see
         *    fs/notify/inotify/inotify_user.c in the kernel tree.
         * Bound-check anyway in case the read came back truncated or
         * the event header was corrupted in flight.
         */
        size_t i = 0;
        while (i + (size_t)event_size <= length)
        {
            e = (struct inotify_event*)&buffer[i];
            if (e->len > length - i - (size_t)event_size)
            {
                errno = EPROTO;
                free(buffer);
                return -1;
            }
            if (-1 == pushChainEvent(ntf, e))
            {
                free(buffer);
                return -1;
            }
            i += (size_t)event_size + e->len;
        }
        free(buffer);
    }

    *path = malloc(1);
    if (*path == NULL)
    {
        return -1;
    }
    (*path)[0] = '\0';

    char* path_watch = watchPath(ntf, e->wd);
    if (path_watch)
    {
        char* built = e->len
            ? lstString("%s/%s", path_watch, e->name)
            : lstString("%s", path_watch);
        if (built == NULL)
        {
            return -1;
        }
        free(*path);
        *path = built;
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
            free(*path);
            freeChainEvent(e);
            return -1;
        }
    }

    if (e->mask & IN_MOVED_FROM
        && e->mask & IN_ISDIR)
    {
        if (path_watch
            && -1 == addCookie(&ntf->cookies, e->wd, path_watch, e->name, e->cookie))
        {
            freeChainEvent(e);
            return -1;
        }
    }

    if (e->mask & IN_MOVED_TO
        && e->mask & IN_ISDIR)
    {
        struct Cookie* C = getCookie(&ntf->cookies, e->cookie);
        if (C && path_watch)
        {
            char* oldpath = lstString("%s/%s", C->path, C->name);
            if (oldpath == NULL)
            {
                freeCookie(C);
                freeChainEvent(e);
                return -1;
            }

            char* newpath = lstString("%s/%s", path_watch, e->name);
            if (newpath == NULL)
            {
                free(oldpath);
                freeCookie(C);
                freeChainEvent(e);
                return -1;
            }

            if (-1 == renameWatches(ntf, oldpath, newpath))
            {
                free(oldpath);
                free(newpath);
                freeCookie(C);
                freeChainEvent(e);
                return -1;
            }

            /* DO NOT REMOVE - this is absolutely necessary */
            if (-1 == addNotify(ntf, newpath, 0))
            {
                free(oldpath);
                free(newpath);
                freeCookie(C);
                freeChainEvent(e);
                return -1;
            }

            free(oldpath);
            free(newpath);
            freeCookie(C);
        }
        else if (C)
        {
            /* cookie matched but the wd is no longer valid; drop it */
            freeCookie(C);
        }
        else
        {
            if (-1 == addNotify(ntf, *path, 0))
            {
                freeChainEvent(e);
                return -1;
            }
        }
    }

    if (e->mask & IN_IGNORED && path_watch)
    {
        /* Drop any pending cookies tied to this wd before the kernel
         * recycles it (IDR may hand the same wd back on the next
         * inotify_add_watch). A stale cookie surviving recycle could
         * collide on cookie value and produce a phantom rename match. */
        dropCookiesForWd(&ntf->cookies, e->wd);
        free(ntf->w[e->wd - 1]);
        ntf->w[e->wd - 1] = NULL;
    }

    if (e->mask & IN_Q_OVERFLOW)
    {
    }

    if (cookie)
    {
        *cookie = e->cookie;
    }
    freeChainEvent(e);

    return 0;
}

/*
 * Public API.
 *
 * Release every resource owned by a Notify: drain the chain, drop
 * pending cookies, release each cached path, remove every watch,
 * close the inotify fd, and free the exclude regex. errno across
 * the call is preserved (some of the cleanup syscalls would
 * clobber it). Safe to pass NULL.
 */
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
        freeChainEvent(e);
    }

    struct Cookie* c = ntf->cookies;
    while (c != NULL)
    {
        struct Cookie* next = c->next;
        freeCookie(c);
        c = next;
    }
    ntf->cookies = NULL;

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

    if (ntf->exclude)
    {
        regfree(ntf->exclude);
    }

    free(ntf->w);
    free(ntf);
    errno = safe_errno;

    return;
}

