#include "config.h"

int unused;

#ifndef HAVE_PLEDGE

int
pledge(const char *promises __unused, const char *execpromises __unused)
{
	return 0;
}

#endif /* !HAVE_PLEDGE */
