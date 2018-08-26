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
	char buf[PATH_MAX];
};

static int maildir_create(struct maildir *);
static int maildir_next(struct maildir *);
static const char *maildir_genname(const struct maildir *,
    const struct maildir *, struct message *, const struct environment *);
static int maildir_read(struct maildir *, char *);
static const char *maildir_root(struct maildir *);

static int parsesubdir(const char *, enum subdir *);
static const char *strsubdir(enum subdir);

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
	} else if (parsesubdir(path, &md->subdir)) {
		maildir_close(md);
		return NULL;
	}
	if ((md->flags & MAILDIR_CREATE) && maildir_create(md)) {
		maildir_close(md);
		return NULL;
	}

	if ((md->flags & MAILDIR_WALK))
		path = pathjoin(md->buf, md->path, strsubdir(md->subdir), NULL);
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

int
maildir_walk(struct maildir *md, char *buf)
{
	const char *path;

	if ((md->flags & MAILDIR_WALK) == 0)
		return 0;

	for (;;) {
		if (maildir_read(md, buf))
			return 1;

		if (maildir_next(md))
			return 0;
		path = pathjoin(md->buf, md->path, strsubdir(md->subdir), NULL);
		if (md->dir != NULL)
			closedir(md->dir);
		md->dir = opendir(path);
		if (md->dir == NULL) {
			warn("opendir: %s", path);
			return 0;
		}
	}
}

int
maildir_move(const struct maildir *src, const struct maildir *dst,
    struct message *msg, const struct environment *env)
{
	char buf[NAME_MAX];
	struct timespec times[2] = {
		{ 0,	UTIME_OMIT },
		{ 0,	0 }
	};
	struct stat st;
	const char *dstname, *path, *srcname;
	int dstfd, srcfd;
	int doutime = 0;

	path = message_get_path(msg);
	srcname = pathslice(path, buf, -1, -1);
	if (srcname == NULL) {
		warnx("%s: basename not found", path);
		return 1;
	}
	srcfd = dirfd(src->dir);
	if (fstatat(srcfd, srcname, &st, 0) != -1) {
		times[1] = st.st_mtim;
		doutime = 1;
	} else {
		warn("fstatat");
	}

	dstname = maildir_genname(src, dst, msg, env);
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
	char buf[PATH_MAX];
	const char *path, *root;

	root = maildir_root(md);
	if (root == NULL) {
		warnx("%s: maildir not found", md->path);
		return 1;
	}

	path = root;
	if (mkdir(path, 0700) == -1 && errno != EEXIST)
		goto err;

	path = pathjoin(buf, root, "cur", NULL);
	if (mkdir(path, 0700) == -1 && errno != EEXIST)
		goto err;

	path = pathjoin(buf, root, "new", NULL);
	if (mkdir(path, 0700) == -1 && errno != EEXIST)
		goto err;

	path = pathjoin(buf, root, "tmp", NULL);
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
    struct message *msg, const struct environment *env)
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
		    ts, getpid(), count, env->hostname, flags);
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

static int
maildir_read(struct maildir *md, char *path)
{
	struct dirent *ent;

	assert(md->dir != NULL);

	for (;;) {
		ent = readdir(md->dir);
		if (ent == NULL)
			return 0;
		if (ent->d_type != DT_REG)
			continue;

		pathjoin(path, md->path, strsubdir(md->subdir), ent->d_name);
		return 1;
	}
}

static const char *
maildir_root(struct maildir *md)
{
	if ((md->flags & MAILDIR_ROOT))
		return md->path;

	return pathslice(md->path, md->buf, 0, -1);
}

static int
parsesubdir(const char *path, enum subdir *subdir)
{
	char buf[NAME_MAX];
	const char *name;

	name = pathslice(path, buf, -1, -1);
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
	warnx("%s: %s: subdir not found", __func__, path);
	return 1;
}

static const char *
strsubdir(enum subdir subdir)
{
	switch (subdir) {
	case SUBDIR_NEW:
		return "new";
	case SUBDIR_CUR:
		return "cur";
	}
	return NULL;
}
