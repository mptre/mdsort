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

enum subdir {
	SUBDIR_NEW,
	SUBDIR_CUR,
};

struct maildir {
	char *path;
	DIR *dir;
	enum subdir subdir;
	int flags;

	/* Internal buffers used to construct directory and file names. */
	char dbuf[PATH_MAX];
	char fbuf[PATH_MAX];
};

static int maildir_create(struct maildir *);
static int maildir_next(struct maildir *);
static const char *maildir_genname(const struct maildir *,
    const struct maildir *, struct message *);
static const char *maildir_read(struct maildir *);
static const char *maildir_root(struct maildir *);

int subdir_parse(const char *, enum subdir *);
const char *subdir_str(enum subdir);

static const char *xbasename(const char *);

struct maildir *
maildir_open(const char *path, int flags)
{
	struct maildir *md;

	md = malloc(sizeof(*md));
	if (md == NULL)
		err(1, NULL);
	md->path = strdup(path);
	if (md->path == NULL)
		err(1, NULL);
	md->dir = NULL;
	md->flags = flags;
	if ((md->flags & MAILDIR_WALK)) {
		md->subdir = SUBDIR_NEW;
	} else if (subdir_parse(path, &md->subdir)) {
		maildir_close(md);
		return NULL;
	}
	if ((md->flags & MAILDIR_CREATE) && maildir_create(md)) {
		maildir_close(md);
		return NULL;
	}

	if ((md->flags & MAILDIR_WALK))
		path = pathjoin(md->dbuf, md->path, subdir_str(md->subdir),
		    NULL);
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

	if ((md->flags & MAILDIR_WALK) == 0)
		return NULL;

	for (;;) {
		path = maildir_read(md);
		if (path != NULL)
			return path;

		if (maildir_next(md))
			return NULL;
		path = pathjoin(md->dbuf, md->path, subdir_str(md->subdir),
		    NULL);
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
    struct message *msg)
{
	struct timespec times[2] = {
		{ 0,	UTIME_OMIT, },
		{ 0,	0 }
	};
	struct stat st;
	const char *dstname, *path, *srcname;
	int dstfd, srcfd;
	int doutime = 0;

	path = message_get_path(msg);
	srcname = xbasename(path);
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

	dstname = maildir_genname(src, dst, msg);
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

static int
maildir_create(struct maildir *md)
{
	const char *path, *root;

	root = maildir_root(md);
	if (root == NULL) {
		warnx("%s: could not find maildir root", md->path);
		return 1;
	}

	path = root;
	if (mkdir(path, 0700) == -1 && errno != EEXIST)
		goto err;

	path = pathjoin(md->dbuf, root, "cur", NULL);
	if (mkdir(path, 0700) == -1 && errno != EEXIST)
		goto err;

	path = pathjoin(md->dbuf, root, "new", NULL);
	if (mkdir(path, 0700) == -1 && errno != EEXIST)
		goto err;

	path = pathjoin(md->dbuf, root, "tmp", NULL);
	if (mkdir(path, 0700) == -1 && errno != EEXIST)
		goto err;

	return 0;

err:
	warn("mkdir: %s", path);
	return 1;
}

static int
maildir_next(struct maildir *md)
{
	switch (md->subdir) {
	case SUBDIR_NEW:
		md->subdir = SUBDIR_CUR;
		return 0;
	case SUBDIR_CUR:
		break;
	}
	return 1;
}

static const char *
maildir_genname(const struct maildir *src, const struct maildir *dst,
    struct message *msg)
{
	static char fname[NAME_MAX];
	const char *flags;
	long long ts;
	int fd, n;
	int count;

	if (src->subdir == SUBDIR_NEW && dst->subdir == SUBDIR_CUR)
		message_set_flags(msg, 'S');
	flags = message_get_flags(msg);

	count = arc4random() % 128;
	for (;;) {
		count++;
		ts = time(NULL);
		n = snprintf(fname, NAME_MAX, "%lld.%d_%d.%s%s",
		    ts, getpid(), count, hostname, flags);
		if (n == -1 || n >= NAME_MAX)
			errx(1, "%s: buffer too small", __func__);
		fd = openat(dirfd(dst->dir), fname, O_WRONLY | O_CREAT | O_EXCL,
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

		return pathjoin(md->fbuf, md->path, subdir_str(md->subdir),
		    ent->d_name);
	}
}

static const char *
maildir_root(struct maildir *md)
{
	const char *subdir;
	int len, n, size;

	if ((md->flags & MAILDIR_ROOT))
		return md->path;

	subdir = subdir_str(md->subdir);
	if (subdir == NULL)
		errx(0, "%s: %s: invalid subdir", __func__, md->path);
	len = strlen(md->path) - strlen(subdir) - 1;
	size = sizeof(md->fbuf);
	n = snprintf(md->fbuf, size, "%.*s", len, md->path);
	if (n == -1 || n >= size)
		errx(1, "%s: buffer too small", __func__);
	return md->fbuf;
}

int
subdir_parse(const char *path, enum subdir *subdir)
{
	const char *name;

	name = xbasename(path);
	if (name == NULL)
		goto err;

	if (strcmp(name, "new") == 0) {
		*subdir = SUBDIR_NEW;
		return 0;
	}
	if (strcmp(name, "cur") == 0) {
		*subdir = SUBDIR_CUR;
		return 0;
	}

err:
	warnx("%s: %s: could not find subdir", __func__, path);
	return 1;
}

const char *
subdir_str(enum subdir subdir)
{
	switch (subdir) {
	case SUBDIR_NEW:
		return "new";
	case SUBDIR_CUR:
		return "cur";
	}
	return NULL;
}

static const char *
xbasename(const char *path)
{
       const char *p;

       p = strrchr(path, '/');
       if (p == NULL || p[1] == '\0')
               return NULL;
       return p + 1;
}
