#include <assert.h>
#include <err.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "extern.h"

static int verbose;

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
		free(str->val);
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

char *
pathslice(const char *path, char *buf, int beg, int end)
{
	const char *p;
	char *bp;
	int docopy, i;
	int isabs = 0;
	int isrange = 1;
	int ncomps = 0;

	if (*path == '/')
		isabs = 1;
	if (!isabs)
		ncomps = 1;	/* compensate for missing leading slash */
	for (p = path; (p = strchr(p, '/')) != NULL; p++)
		ncomps++;

	if (end - beg == 0)
		isrange = 0;

	if (end < 0)
		end = ncomps + end - isrange;
	if (beg < 0)
		beg = ncomps + beg - isrange;
	if (beg < 0 || beg > end || end < 0 || end >= ncomps)
		return NULL;

	p = path;
	bp = buf;
	for (i = 0; i < ncomps; i++) {
		if (*p == '\0')
			break;

		docopy = i >= beg && i <= end;
		if (docopy) {
			if (isabs && isrange)
				*bp++ = '/';
			else if (!isabs)
				*bp++ = *p;
		}
		isabs = 1;
		for (p++; *p != '/' && *p != '\0'; p++) {
			if (docopy)
				*bp++ = *p;
		}
	}
	*bp = '\0';

	return buf;
}

void
log_init(int v)
{
	verbose = v;
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
