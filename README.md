# librnotify v2.0.0

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
- GCC or compatible compiler
- libcurl and json-c libraries

### Building the Library

```bash
git clone https://github.com/zmushko/librnotify.git
cd librnotify
make
```

## Usage

### Basic Example

```c
#include <stdio.h>
#include <stdlib.h>
#include "rnotify.h"

int main(int argc, char** argv) {
    // Paths to watch (NULL-terminated array)
    char* paths[] = {"/path/to/watch", NULL};
    
    // Initialize notification system
    // IN_ALL_EVENTS can be replaced with specific events like IN_CREATE|IN_MODIFY
    Notify* ntf = initNotify(paths, IN_ALL_EVENTS, NULL);
    if (!ntf) {
        perror("Failed to initialize notification system");
        return 1;
    }
    
    // Event loop
    char* path = NULL;
    uint32_t mask = 0;
    uint32_t cookie = 0;
    
    while (0 == waitNotify(ntf, &path, &mask, -1, &cookie)) {
        printf("Event: %s (mask: %08x, cookie: %u)\n", path, mask, cookie);
        free(path); // Important: free the path after use!
    }
    
    // Cleanup
    freeNotify(ntf);
    return 0;
}
```

### Compile Your Program

```bash
gcc -o my_program my_program.c -L/path/to/librnotify -lrnotify -lcurl -ljson-c
```

## API Reference

### `Notify* initNotify(char** path, const uint32_t mask, const char* exclude)`

Initializes the notification system.

- **path**: NULL-terminated array of paths to monitor
- **mask**: Bit mask of events to monitor (see inotify constants)
- **exclude**: Regex pattern to exclude certain files/directories (NULL for no exclusion)
- **returns**: Notify pointer on success, NULL on failure

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

Maintained by Volodymyr Mushko (https://github.com/zmushko)
