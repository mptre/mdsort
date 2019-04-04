#include "config.h"

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

static int verbose;

/*
 * Allocate a list of strings.
 *
 * The caller is responsible for freeing the returned memory using
 * strings_free().
 */
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

/*
 * Join root, dirname and filename into a path written to buf which must be at
 * least of size PATH_MAX.
 * The filename may optionally be NULL.
 */
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
	if (n < 0 || n >= PATH_MAX)
		errc(1, ENAMETOOLONG, "%s", __func__);
	return buf;
}

/*
 * Writes the given number of components from path to buf which must be at least
 * of size PATH_MAX.
 * The component range as given by beg and end may either be positive (start
 * from the beginning) or negative (start from the end).
 * If beg is equal to end, only a single component of the path is extract.
 */
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

int
nspaces(const char *str)
{
	int n = 0;

	for (; *str == ' ' || *str == '\t'; str++)
		n++;
	return n;
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
