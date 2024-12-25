#include "util.h"

#include "config.h"

#include <sys/wait.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int	log_level = 0;

/*
 * Execute external command. If fdin is equal to -1, /dev/null will be used as
 * standard input. Returns one of the following:
 *
 *     >0    Command exited non-zero.
 *      0    Command exited zero.
 *     <0    Fatal error.
 */
int
exec(const char **argv, int fdin)
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
		union {
			const char	**src;
			char *const	 *dst;
		} u = {.src = argv};

		if (dup2(fdin, 0) == -1)
			err(1, "dup2");
		execvp(argv[0], u.dst);
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

int
isstdin(const char *str)
{
	return strcmp(str, "/dev/stdin") == 0;
}
