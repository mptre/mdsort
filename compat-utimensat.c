#include "config.h"

int unused;

#ifndef HAVE_UTIMENSAT

#include <sys/stat.h>
#include <sys/time.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int
utimensat(int fd, const char *path __unused, const struct timespec times[2],
    int flags __unused)
{
	struct stat st;
	struct timeval tv[2];
	const struct timespec *atim, *mtim;
	int errno_save, res, rfd;

	/* Assume path is relative to fd. */
	rfd = openat(fd, path, O_RDONLY | O_CLOEXEC);
	if (rfd == -1)
		return -1;

	if ((times[0].tv_nsec == UTIME_OMIT || times[1].tv_nsec == UTIME_OMIT)
	    && fstat(rfd, &st) == -1) {
		res = -1;
		goto ret;
	}

	if (times[0].tv_nsec == UTIME_OMIT)
		atim = &st.st_atim;
	else
		atim = &times[0];
	tv[0].tv_sec = atim->tv_sec;
	tv[0].tv_usec = atim->tv_nsec / 1000;

	if (times[1].tv_nsec == UTIME_OMIT)
		mtim = &st.st_mtim;
	else
		mtim = &times[1];
	tv[1].tv_sec = mtim->tv_sec;
	tv[1].tv_usec = mtim->tv_nsec / 1000;

	res = futimes(rfd, tv);
ret:
	errno_save = errno;
	close(rfd);
	errno = errno_save;
	return res;
}

#endif /* !UTIMENSAT */
