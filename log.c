#include "log.h"

#include "config.h"

#include <stdarg.h>
#include <stdio.h>

void
logit(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	fflush(stdout);
}
