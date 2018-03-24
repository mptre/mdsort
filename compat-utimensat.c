#include "config.h"

int unused;

#ifndef HAVE_UTIMENSAT

#include <sys/time.h>

int
utimensat(int fd, const char *path __attribute__((__unused__)),
    const struct timespec times[2], int flags __attribute__((__unused__)))
{
	struct timeval tv[2];
	struct timeval now;

	/* XXX not how utimensat() should behave */
	if (times[0].tv_nsec == UTIME_OMIT) {
		if (gettimeofday(&now, NULL) == -1)
			return -1;
		tv[0] = now;
	} else {
		tv[0].tv_sec = times[0].tv_sec;
		tv[0].tv_usec = times[0].tv_nsec / 1000;
	}

	if (times[1].tv_nsec == UTIME_OMIT) {
		if (gettimeofday(&now, NULL) == -1)
			return -1;
		tv[1] = now;
	} else {
		tv[1].tv_sec = times[1].tv_sec;
		tv[1].tv_usec = times[1].tv_nsec / 1000;
	}

	return futimes(fd, tv);
}

#endif /* !UTIMENSAT */
