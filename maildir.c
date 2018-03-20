#include "config.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "extern.h"

enum {
	MAILDIR_NEW = 1,
	MAILDIR_CUR,
};

struct maildir {
	char *path;
	DIR *dir;
	int dirname;

	/* Internal buffers used to construct directory and file names. */
	char dbuf[PATH_MAX];
	char fbuf[PATH_MAX];
};

static const char *maildir_genname(const struct maildir *, const char *);
static const char *maildir_read(struct maildir *);

static const char *basename(const char *);
static const char *dirname(int);
static const char *pathexpand(const char *, const struct rule_match *);
static int pathjoin(char *, const char *, const char *, const char *);

struct maildir *
maildir_open(const char *path, int nowalk)
{
	struct maildir *md;

	md = malloc(sizeof(*md));
	if (md == NULL)
		err(1, NULL);
	md->path = strdup(path);
	if (md->path == NULL)
		err(1, NULL);
	md->dirname = nowalk;
	if (md->dirname == 0)
		md->dirname = MAILDIR_NEW;

	if (pathjoin(md->dbuf, md->path, dirname(md->dirname), NULL)) {
		warnx("unknown maildir path");
		maildir_close(md);
		return NULL;
	}
	md->dir = opendir(md->dbuf);
	if (md->dir == NULL) {
		warn("%s", md->dbuf);
		maildir_close(md);
		return NULL;
	}

	return md;
}

struct maildir *
maildir_openat(const struct maildir *md, const char *path,
    const struct rule_match *match)
{
	const char *p;

	if ((p = pathexpand(path, match)) == NULL)
		return NULL;
	return maildir_open(p, md->dirname);
}

void
maildir_close(struct maildir *md)
{
	if (md == NULL)
		return;
	if (md->dir != NULL)
		closedir(md->dir);
	free(md->path);
	free(md);
}

const char *
maildir_walk(struct maildir *md)
{
	const char *path;

	for (;;) {
		if ((path = maildir_read(md)) != NULL)
			return path;

		md->dirname++;
		if (pathjoin(md->dbuf, md->path, dirname(md->dirname), NULL))
			return NULL;
		if (md->dir != NULL)
			closedir(md->dir);
		md->dir = opendir(md->dbuf);
		if (md->dir == NULL) {
			warn("%s", md->dbuf);
			return NULL;
		}
	}
}

int
maildir_move(const struct maildir *src, const struct maildir *dst,
    const char *path)
{
	struct timespec times[2] = {
		{ 0,	UTIME_OMIT, },
		{ 0,	0 }
	};
	struct stat st;
	const char *dstname, *srcname;
	int dstfd, srcfd;
	int doutime = 0;

	srcname = basename(path);
	if (srcname == NULL) {
		warnx("%s: could not extract basename", path);
		return 1;
	}
	srcfd = dirfd(src->dir);
	if (fstatat(srcfd, srcname, &st, 0) != -1) {
		times[1] = st.st_mtim;
		doutime = 1;
	} else {
		warn("fstatat");
	}

	dstname = maildir_genname(dst, srcname);
	dstfd = dirfd(dst->dir);

	if (renameat(srcfd, srcname, dstfd, dstname) == -1) {
		warn("renameat");
		return 1;
	} else if (doutime && utimensat(dstfd, dstname, times, 0) == -1) {
		warn("utimensat");
		return 1;
	}
	return 0;
}

const char *
maildir_get_path(const struct maildir *md)
{
	return md->dbuf;
}

static const char *
maildir_genname(const struct maildir *md, const char *from)
{
	static char fname[NAME_MAX];
	const char *flags;
	long long ts;
	int fd, n;
	int count;

	if ((flags = strchr(from, ':')) == NULL || flags[1] == '\0')
		flags = "";

	count = arc4random() % 128;
	for (;;) {
		count++;
		ts = time(NULL);
		n = snprintf(fname, NAME_MAX, "%lld.%d_%d.%s%s",
		    ts, getpid(), count, hostname, flags);
		if (n == -1 || n >= NAME_MAX)
			errx(1, "%s: buffer too small", __func__);
		fd = openat(dirfd(md->dir), fname, O_WRONLY | O_CREAT | O_EXCL,
		    0666);
		if (fd == -1) {
			if (errno == EEXIST) {
				log_debug("%s: %s: file exists\n",
				    __func__, fname);
				continue;
			}
			err(1, "openat: %s", fname);
		}
		close(fd);
		return fname;
	}
}

static const char *
maildir_read(struct maildir *md)
{
	struct dirent *ent;

	assert(md->dir != NULL);

	for (;;) {
		ent = readdir(md->dir);
		if (ent == NULL)
			return NULL;
		if (ent->d_type != DT_REG)
			continue;

		if (pathjoin(md->fbuf, md->path, dirname(md->dirname),
			    ent->d_name))
			return NULL;
		return md->fbuf;
	}
}

static const char *
basename(const char *path)
{
	const char *p;

	p = strrchr(path, '/');
	if (p == NULL)
		return NULL;
	if (strlen(p + 1) == 0)
		return NULL;
	return p + 1;
}

static const char *
dirname(int dir)
{
	switch (dir) {
	case MAILDIR_NEW:
		return "new";
	case MAILDIR_CUR:
		return "cur";
	default:
		return NULL;
	}
}

static const char *
pathexpand(const char *path, const struct rule_match *match)
{
	static char buf[PATH_MAX];
	const char *sub;
	char *end;
	unsigned long mid;
	size_t i = 0;
	size_t j = 0;

	while (path[i] != '\0') {
		if (i > 0 && path[i - 1] == '\\' && isdigit(path[i])) {
			errno = 0;
			mid = strtoul(path + i, &end, 10);
			if ((errno == ERANGE && mid == ULONG_MAX) ||
			    ((sub = rule_match_get(match, mid)) == NULL))
				goto err2;
			/* Adjust j to remove previously copied backslash. */
			j--;
			for (; *sub != '\0'; sub++) {
				if (j == sizeof(buf) - 1)
					goto err1;
				buf[j++] = *sub;
			}
			i = end - path;
			continue;
		}
		if (j == sizeof(buf) - 1)
			goto err1;
		buf[j++] = path[i++];
	}
	assert(j < sizeof(buf));
	buf[j] = '\0';
	return buf;

err1:
	warnx("%s: destination too long", path);
	return NULL;
err2:
	warnx("%s: invalid back-reference in destination", path);
	return NULL;
}

static int
pathjoin(char *buf, const char *root, const char *dirname, const char *filename)
{
	int n;

	if (root == NULL || dirname == NULL)
		return 1;

	if (filename == NULL)
		n = snprintf(buf, PATH_MAX, "%s/%s", root, dirname);
	else
		n = snprintf(buf, PATH_MAX, "%s/%s/%s",
		    root, dirname, filename);
	if (n == -1 || n >= PATH_MAX)
		err(1, "%s: buffer too small", __func__);
	return 0;
}
