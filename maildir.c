#include "config.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <assert.h>
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

enum maildir_dirname {
	MAILDIR_NEW = 1,
	MAILDIR_CUR,
};

struct maildir {
	char *path;
	DIR *dir;
	enum maildir_dirname dirname;

	/* Internal buffers used to construct directory and file names. */
	char dbuf[PATH_MAX];
	char fbuf[PATH_MAX];
};

static const char *maildir_dirname(const struct maildir *);
static int maildir_dirnext(struct maildir *);
static const char *maildir_genname(const struct maildir *, const char *);
static const char *maildir_read(struct maildir *);

static char *pathjoin(char *, const char *, const char *, const char *);

struct maildir *
maildir_open(const char *path, int walk)
{
	struct maildir *md;

	md = malloc(sizeof(*md));
	if (md == NULL)
		err(1, NULL);
	md->path = strdup(path);
	if (md->path == NULL)
		err(1, NULL);
	md->dir = NULL;
	md->dirname = 0;
	if (walk) {
		md->dirname = MAILDIR_NEW;
		path = pathjoin(md->dbuf, md->path, maildir_dirname(md), NULL);
	}
	md->dir = opendir(path);
	if (md->dir == NULL) {
		warn("opendir: %s", path);
		maildir_close(md);
		return NULL;
	}

	return md;
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

	if (md->dirname == 0)
		return NULL;

	for (;;) {
		path = maildir_read(md);
		if (path != NULL)
			return path;

		if (maildir_dirnext(md))
			return NULL;
		path = pathjoin(md->dbuf, md->path, maildir_dirname(md), NULL);
		if (md->dir != NULL)
			closedir(md->dir);
		md->dir = opendir(path);
		if (md->dir == NULL) {
			warn("opendir: %s", path);
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

	/* Increment srcname to skip leading '/' */
	if ((srcname = strrchr(path, '/')) == NULL || strlen(++srcname) == 0) {
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

static const char *
maildir_dirname(const struct maildir *md)
{
	switch (md->dirname) {
	case MAILDIR_NEW:
		return "new";
	case MAILDIR_CUR:
		return "cur";
	}
	return NULL;
}

static int
maildir_dirnext(struct maildir *md)
{
	switch (md->dirname) {
	case MAILDIR_NEW:
		md->dirname = MAILDIR_CUR;
		return 0;
	case MAILDIR_CUR:
		break;
	}
	return 1;
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

		return pathjoin(md->fbuf, md->path, maildir_dirname(md),
		    ent->d_name);
	}
}

static char *
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
