#include "config.h"

int unused;

#ifndef HAVE_PLEDGE

#include <time.h>

#include "extern.h"

int
pledge(const char *UNUSED(promises), const char *UNUSED(execpromises))
{
	return 0;
}

#endif /* !HAVE_PLEDGE */
