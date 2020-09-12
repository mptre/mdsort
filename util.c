#include "config.h"

#include <assert.h>
#include <err.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

int log_level = 0;

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

size_t
strings_len(const struct string_list *strings)
{
	const struct string *str;
	size_t len = 0;

	TAILQ_FOREACH(str, strings, entry) {
		len++;
	}
	return len;
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
 * Join dirname and filename into a path written to buf.
 */
char *
pathjoin(char *buf, size_t bufsiz, const char *dirname, const char *filename)
{
	int n;

	n = snprintf(buf, bufsiz, "%s/%s", dirname, filename);
	if (n < 0 || (size_t)n >= bufsiz)
		return NULL;
	return buf;
}

/*
 * Writes the given number of components from path to buf.
 * The component range as given by beg and end may either be positive (start
 * from the beginning) or negative (start from the end).
 * If beg is equal to end, only a single component of the path is extract.
 */
char *
pathslice(const char *path, char *buf, size_t bufsiz, int beg, int end)
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
			if (bufsiz == 0)
				return NULL;
			if (isabs && isrange) {
				*bp++ = '/';
				bufsiz--;
			} else if (!isabs) {
				*bp++ = *p;
				bufsiz--;
			}
		}
		isabs = 1;
		for (p++; *p != '/' && *p != '\0'; p++) {
			if (!docopy)
				continue;
			if (bufsiz == 0)
				return NULL;

			*bp++ = *p;
			bufsiz--;
		}
	}
	if (bufsiz == 0)
		return NULL;
	*bp = '\0';

	return buf;
}

size_t
nspaces(const char *str)
{

	return strspn(str, " \t");
}

/*
 * Append str, including the NUL-terminator, to buf. Both bufsiz and buflen are
 * updated accordingly. Returns the number of appended bytes, excluding the
 * NUL-terminator.
 */
size_t
append(char **buf, size_t *bufsiz, size_t *buflen, const char *str)
{
	size_t len;

	len = strlen(str);
	while (*buflen + len + 1 >= *bufsiz) {
		size_t newsiz;

		newsiz = 2 * *bufsiz;
		if (newsiz == 0)
			newsiz = 128;
		*buf = realloc(*buf, newsiz);
		if (*buf == NULL)
			err(1, NULL);
		*bufsiz = newsiz;
	}
	memcpy(*buf + *buflen, str, len + 1);
	*buflen += len;

	return len;
}

/*
 * Append ch to buf using the semantics as append().
 */
size_t
appendc(char **buf, size_t *bufsiz, size_t *buflen, char ch)
{
	char str[2];

	str[0] = ch;
	str[1] = '\0';
	return append(buf, bufsiz, buflen, str);
}

void
_log_debug(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	fflush(stdout);
}

void
_log_info(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	fflush(stdout);
}
