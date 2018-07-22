#include "config.h"

#include <assert.h>
#include <err.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "extern.h"

struct string_list *
strings_alloc(void)
{
	struct string_list *strings;

	strings = malloc(sizeof(*strings));
	if (strings == NULL)
		err(1, NULL);
	TAILQ_INIT(strings);
	return strings;
}

void
strings_free(struct string_list *strings)
{
	struct string *str;

	if (strings == NULL)
		return;

	while ((str = TAILQ_FIRST(strings)) != NULL) {
		TAILQ_REMOVE(strings, str, entry);
		free(str);
	}
	free(strings);
}

void
strings_append(struct string_list *strings, char *val)
{
	struct string *str;

	str = malloc(sizeof(*str));
	if (str == NULL)
		err(1, NULL);
	str->val = val;
	TAILQ_INSERT_TAIL(strings, str, entry);
}

char *
pathjoin(char *buf, const char *root, const char *dirname, const char *filename)
{
	int n;

	assert(root != NULL);
	assert(dirname != NULL);

	if (filename == NULL)
		n = snprintf(buf, PATH_MAX, "%s/%s", root, dirname);
	else
		n = snprintf(buf, PATH_MAX, "%s/%s/%s",
		    root, dirname, filename);
	if (n == -1 || n >= PATH_MAX)
		errx(1, "%s: buffer too small", __func__);
	return buf;
}

void
log_debug(const char *fmt, ...)
{
	va_list ap;

	if (verbose < 2)
		return;
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
}

void
log_info(const char *fmt, ...)
{
	va_list ap;

	if (verbose < 1)
		return;
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
}
