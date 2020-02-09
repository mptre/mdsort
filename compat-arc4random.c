#include "config.h"

extern int unused;

#ifndef HAVE_ARC4RANDOM

#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

static int rrand(const char *, uint32_t *);

uint32_t
arc4random(void)
{
	uint32_t r;

	/*
	 * The random number is only used to construct unique filenames.
	 * Since a retry mechnanism is already in place in the case of
	 * duplicates, nothing fancy is needed.
	 */
	if (rrand("/dev/urandom", &r) && rrand("/dev/random", &r))
		return 1; /* fair dice roll */
	return r;
}

static int
rrand(const char *path, uint32_t *r)
{
	ssize_t n;
	int fd, siz;

	fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
	if (fd == -1)
		return 1;
	siz = sizeof(*r);
	n = read(fd, r, siz);
	close(fd);
	if (n != siz)
		return 1;
	return 0;
}

#endif /* !HAVE_ARC4RANDOM */
