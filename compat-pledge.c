#include "config.h"

extern int unused;

#ifndef HAVE_PLEDGE

#include <stdint.h>
#include <unistd.h>

#include "extern.h"

int
pledge(const char *UNUSED(promises), const char *UNUSED(execpromises))
{
	return 0;
}

#endif
