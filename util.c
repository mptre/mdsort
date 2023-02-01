#include "config.h"

#include <sys/wait.h>

#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

int	log_level = 0;

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

	TAILQ_FOREACH(str, strings, entry)
		len++;
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
    size_t *buflen, char delim)
{
	const struct string *str;
	int nmemb = 0;

	if (buf != NULL && buf[0] != '\0')
		nmemb = 1;

	TAILQ_FOREACH(str, strings, entry) {
		if (delim != 0 && nmemb > 0)
			appendc(&buf, bufsiz, buflen, delim);
		if (append(&buf, bufsiz, buflen, str->val))
			nmemb++;
	}
	return buf;
}

/*
 * Execute external command. If fdin is equal to -1, /dev/null will be used as
 * standard input. Returns one of the following:
 *
 *     >0    Command exited non-zero.
 *      0    Command exited zero.
 *     <0    Fatal error.
 */
int
exec(char *const *argv, int fdin)
{
	pid_t pid;
	int error = 1;
	int doclose = 0;
	int status;

	if (fdin == -1) {
		doclose = 1;
		fdin = open("/dev/null", O_RDONLY | O_CLOEXEC);
		if (fdin == -1) {
			warn("open: /dev/null");
			error = -1;
			goto out;
		}
	}

	pid = fork();
	if (pid == -1) {
		warn("fork");
		error = -1;
		goto out;
	}
	if (pid == 0) {
		if (dup2(fdin, 0) == -1)
			err(1, "dup2");
		execvp(argv[0], argv);
		warn("%s", argv[0]);
		_exit(127);
	}

	if (waitpid(pid, &status, 0) == -1) {
		warn("waitpid");
		error = -1;
		goto out;
	}
	if (WIFEXITED(status)) {
		error = WEXITSTATUS(status);
		if (error == 127)
			error = -1;
	}
	if (WIFSIGNALED(status))
		error = 128 + WTERMSIG(status);

out:
	if (doclose && fdin != -1)
		close(fdin);
	return error;
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
	int isabs = 0;
	int isrange = 1;
	int ncomps = 0;
	int i;

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
		int docopy;

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
	return strcmp(str, "/dev/stdin") == 0;
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
