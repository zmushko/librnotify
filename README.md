# librnotify v3.0.0

A C library for efficient recursive directory monitoring using Linux's inotify API.

## Features

- **Recursive Directory Watching**: Automatically monitors all subdirectories within watched paths
- **Event Filtering**: Configure which event types to monitor (create, modify, delete, etc.)
- **Path Exclusion**: Ignore files/directories using regex patterns
- **Efficient Resource Management**: Clean handling of inotify resources
- **Cookie Tracking**: Properly tracks file/directory move operations
- **Extensive Event Types**: Support for all inotify event types

## Installation

### Prerequisites

- Linux system with inotify support
- C11-capable compiler (GCC ≥ 4.6 or Clang ≥ 3.3)
- GNU make

No third-party libraries are required; the library only links libc.

### Building the Library

```bash
git clone https://github.com/zmushko/librnotify.git
cd librnotify
make
sudo make install                  # installs under /usr/local by default
# Override the layout if needed:
sudo make PREFIX=/usr install
```

`make` produces a shared library (`librnotify.so` plus the SONAME-versioned
symlinks), a static archive (`librnotify.a`), and a `pkg-config` file
(`librnotify.pc`).

### Running the test suite

```bash
make check
```

builds a small event reporter and runs every shell script in `tests/`
against it (race-free recursive watching, atomic-save cookie pairing,
symlink no-follow, recursive directory move). The suite requires a
Linux host with inotify.

```bash
make sanitize
```

rebuilds everything with AddressSanitizer + UndefinedBehaviorSanitizer
and links the `test` binary against the instrumented archive — useful
when investigating failures surfaced by `make check`.

## Usage

### Basic Example

```c
#include <stdio.h>
#include <stdlib.h>
#include "rnotify.h"

int main(int argc, char** argv) {
    // Watch a single directory tree.
    // IN_ALL_EVENTS can be replaced with specific bits, e.g. IN_CREATE|IN_MODIFY.
    Notify* ntf = initNotify("/path/to/watch", IN_ALL_EVENTS, NULL);
    if (!ntf) {
        perror("Failed to initialize notification system");
        return 1;
    }

    // Event loop. To watch additional roots, create more Notify
    // instances and select() over notifyFd() of each.
    for (;;) {
        char*    path   = NULL;
        uint32_t mask   = 0;
        uint32_t cookie = 0;
        int rc = waitNotify(ntf, &path, &mask, -1, &cookie);
        if (rc != 0) {
            // -1 = error (errno set); positive value = timeout fired.
            break;
        }
        printf("Event: %s (mask: %08x, cookie: %u)\n", path, mask, cookie);
        free(path); // Important: free the path after use!
    }

    freeNotify(ntf);
    return 0;
}
```

### Compile Your Program

After `make install`, link against the system-installed library:

```bash
gcc -o my_program my_program.c $(pkg-config --cflags --libs librnotify)
```

Or, when linking against a local in-tree build:

```bash
gcc -o my_program my_program.c -I/path/to/librnotify -L/path/to/librnotify -lrnotify
```

## API Reference

### `Notify* initNotify(const char* path, const uint32_t mask, const char* exclude)`

Initializes a notification monitor on a single directory tree.

- **path**: absolute path to monitor (recursive descent is automatic)
- **mask**: bit mask of events to monitor (see inotify constants).
  `IN_DONT_FOLLOW` is added unconditionally — symlinks are never
  dereferenced.
- **exclude**: POSIX extended regex; events whose `name` matches are
  dropped before reaching the consumer (NULL for no exclusion)
- **returns**: `Notify*` on success, `NULL` with `errno` set on
  failure (`EINVAL` for NULL path, `ENOENT` if the path does not
  exist, or any errno from `inotify_init`/`inotify_add_watch`/
  `malloc`/`regcomp`).

To watch multiple roots, create one `Notify` per root and integrate
`notifyFd()` of each into your own `select()`/`poll()`/`epoll()` loop.

### `int notifyFd(const Notify* ntf)`

Returns the underlying inotify file descriptor for integration with
the caller's event loop. The fd is owned by the `Notify`; do not
close it or read from it directly — always go through `waitNotify`.

- **returns**: fd on success, `-1` with `errno = EINVAL` on NULL input.

### `int waitNotify(Notify* ntf, char** path, uint32_t* mask, const int timeout, uint32_t* cookie)`

Waits for the next notification event.

- **ntf**: The Notify pointer returned by initNotify
- **path**: Pointer to receive the path where the event occurred (will be malloc'd)
- **mask**: Pointer to receive the event type mask
- **timeout**: Timeout in milliseconds (-1 for indefinite)
- **cookie**: Pointer to receive the cookie value (for tracking move operations)
- **returns**: 0 on success, timeout value on timeout, -1 on error

### `void freeNotify(Notify* ntf)`

Cleans up resources used by the notification system.

- **ntf**: The Notify pointer returned by initNotify

## Event Types

- `IN_ACCESS`: File was accessed
- `IN_MODIFY`: File was modified
- `IN_ATTRIB`: Metadata changed
- `IN_CLOSE_WRITE`: Writable file was closed
- `IN_CLOSE_NOWRITE`: Non-writable file closed
- `IN_OPEN`: File was opened
- `IN_MOVED_FROM`: File was moved from X
- `IN_MOVED_TO`: File was moved to Y
- `IN_CREATE`: File/directory created
- `IN_DELETE`: File/directory deleted
- `IN_DELETE_SELF`: Self was deleted
- `IN_MOVE_SELF`: Self was moved
- `IN_ALL_EVENTS`: All events

## License

See LICENSE.md file for details.

## Author

Maintained by Andrey Zmushko (https://github.com/zmushko)
Email: andrey.zmushko@gmail.com
