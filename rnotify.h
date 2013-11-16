#include <sys/inotify.h>
#include <regex.h>

typedef struct {
	int fd;
	unsigned int size_w;
	char** w;
	long max_name;
	unsigned long max_queued_events;
	uint32_t mask;
	regex_t* exclude;
	struct chainEvent* head;
	struct chainEvent* tail;
} Notify;

Notify*	initNotify(const char* path, const uint32_t mask, const char* exclude);
int	waitNotify(Notify* ntf, char** const path, uint32_t* mask);
void	freeNotify(Notify* ntf);

