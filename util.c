#include "config.h"

#include <assert.h>
#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

int		 log_level = 0;
const char	*stdinpath = "/dev/stdin";

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
		if ((str->flags & STRING_FLAG_CONST) == 0)
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

struct string *
strings_append(struct string_list *strings, char *val)
{
	struct string *str;

	str = malloc(sizeof(*str));
	if (str == NULL)
		err(1, NULL);
	str->val = val;
	str->flags = 0;
	TAILQ_INSERT_TAIL(strings, str, entry);
	return str;
}

struct string *
strings_appendc(struct string_list *strings, const char *val)
{
	struct string *str;

	str = strings_append(strings, (char *)val);
	str->flags |= STRING_FLAG_CONST;
	return str;
}

char *
strings_concat(const struct string_list *strings, char *buf, size_t *bufsiz,
    size_t *buflen)
{
	const struct string *str;
	int nmemb = 0;

	if (buf != NULL && buf[0] != '\0')
		nmemb = 1;

	TAILQ_FOREACH(str, strings, entry) {
		if (nmemb > 0)
			appendc(&buf, bufsiz, buflen, ' ');
		if (append(&buf, bufsiz, buflen, str->val))
			nmemb++;
	}
	return buf;
}

void
macros_init(struct macro_list *macros, unsigned int ctx)
{
	memset(macros, 0, sizeof(*macros));
	macros->ml_size = sizeof(macros->ml_v) / sizeof(macros->ml_v[0]);
	macros->ml_ctx = ctx;
	TAILQ_INIT(&macros->ml_list);
}

enum macro_error
macros_insert(struct macro_list *macros, char *name, char *value,
    unsigned int flags, int lno)
{
	struct macro *mc;

	if ((macro_context(name) & macros->ml_ctx) == 0)
		return MACRO_ERR_CTX;
	if ((mc = macros_find(macros, name)) != NULL) {
		if (mc->mc_flags & MACRO_FLAG_STICKY) {
			if ((flags & MACRO_FLAG_STICKY) || ++mc->mc_defs > 1)
				return MACRO_ERR_EXIST;
			return MACRO_ERR_STICKY;
		}
		return MACRO_ERR_EXIST;
	}

	if (macros->ml_nmemb < macros->ml_size) {
		mc = &macros->ml_v[macros->ml_nmemb++];
		flags |= MACRO_FLAG_STATIC;
	} else {
		mc = malloc(sizeof(*mc));
		if (mc == NULL)
			err(1, NULL);
	}

	mc->mc_name = name;
	mc->mc_value = value;
	mc->mc_refs = 0;
	mc->mc_defs = 0;
	mc->mc_lno = lno;
	mc->mc_flags = flags;
	TAILQ_INSERT_TAIL(&macros->ml_list, mc, mc_entry);
	return MACRO_ERR_NONE;
}

/*
 * Insert a constant macro, only used for non-default contexts.
 * Note, the macro list does not claim memory ownership of the given name and
 * value thus the caller must ensure that the lifetime of the two aforementioned
 * pointers supersedes the macro list.
 */
void
macros_insertc(struct macro_list *macros, const char *name, const char *value)
{
	enum macro_error error;

	/*
	 * A macro list used in a non-default context is always stack allocated.
	 * Therefore ensure that the stack storage is sufficient.
	 */
	if (macros->ml_nmemb == macros->ml_size)
		errx(1, "%s: stack storage exhausted", __func__);

	/* Dangerous casting ahead but a macro is never mutated. */
	error = macros_insert(macros, (char *)name, (char *)value,
	    MACRO_FLAG_CONST, 0);
	if (error)
		errx(1, "%s: error %d", __func__, error);
}

struct macro *
macros_find(const struct macro_list *macros, const char *name)
{
	struct macro *mc;

	TAILQ_FOREACH(mc, &macros->ml_list, mc_entry) {
		if (strcmp(mc->mc_name, name) == 0)
			return mc;
	}
	return NULL;
}

unsigned int
macro_context(const char *name)
{
	static struct {
		const char	*name;
		unsigned int	 ctx;
	} macros[] = {
		{ "path",	MACRO_CTX_ACTION },

		{ NULL,	MACRO_CTX_DEFAULT },
	};
	int i;

	for (i = 0; macros[i].name != NULL; i++) {
		if (strcmp(macros[i].name, name) == 0)
			return macros[i].ctx;
	}
	return MACRO_CTX_DEFAULT;
}

/*
 * Determine if the given string starts with a macro. Returns one of the
 * following:
 *
 *     >0   The length of the found macro. The caller is responsible for freeing
 *          the memory pointed to in name.
 *
 *     0    Macro not found.
 *
 *     -1   Unterminated macro found.
 */
ssize_t
ismacro(const char *str, char **macro)
{
	size_t i;

	if (str[0] != '$' || str[1] != '{')
		return 0;

	for (i = 2; str[i] != '}'; i++) {
		if (str[i] == '\0')
			return -1;
	}

	*macro = strndup(&str[2], i - 2);
	if (*macro == NULL)
		err(1, NULL);
	return i + 1;
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
	if (*buflen + len + 1 >= *bufsiz) {
		size_t newsiz;

		newsiz = *bufsiz > 0 ? 2 * *bufsiz : 64;
		while (*buflen + len + 1 >= newsiz)
			newsiz *= 2;
		*buf = reallocarray(*buf, 1, newsiz);
		if (*buf == NULL)
			err(1, NULL);
		*bufsiz = newsiz;
	}

	memcpy(*buf + *buflen, str, len + 1);
	*buflen += len;
	return len;
}

/*
 * Append ch to buf using the same semantics as append().
 */
size_t
appendc(char **buf, size_t *bufsiz, size_t *buflen, char ch)
{
	char str[2];

	str[0] = ch;
	str[1] = '\0';
	return append(buf, bufsiz, buflen, str);
}

int
isstdin(const char *str)
{
	return str == stdinpath;
}

void
logit(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	fflush(stdout);
}
