#include "config.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

static char *maildir_genname(const struct maildir *, const char *,
    char *, size_t, const struct environment *);
static int maildir_next(struct maildir *);
static int maildir_opendir(struct maildir *, const char *);
static int maildir_stdin(struct maildir *, const struct environment *);
static const char *maildir_path(struct maildir *, const char *);
static const char *maildir_read(struct maildir *);

static const char *msgflags(const struct maildir *, const struct maildir *,
    struct message *);
static int parsesubdir(const char *, enum subdir *);

struct maildir *
maildir_open(const char *path, int flags, const struct environment *env)
{
	struct maildir *md;

	md = calloc(1, sizeof(*md));
	if (md == NULL)
		err(1, NULL);
	md->subdir = SUBDIR_NEW;
	md->flags = flags;

	if ((md->flags & MAILDIR_STDIN)) {
		if (maildir_stdin(md, env))
			goto fail;
	} else {
		if ((md->flags & MAILDIR_WALK)) {
			md->path = strdup(path);
			if (md->path == NULL)
				err(1, NULL);
		} else {
			if (parsesubdir(path, &md->subdir))
				goto fail;

			md->path = malloc(PATH_MAX);
			if (md->path == NULL)
				err(1, NULL);
			if (pathslice(path, md->path, 0, -1) == NULL)
				goto fail;
		}
	}

	path = maildir_path(md, NULL);
	if (maildir_opendir(md, path))
		goto fail;

	return md;

fail:
	maildir_close(md);
	return NULL;
}

void
maildir_close(struct maildir *md)
{
	const char *dir, *path;

	if (md == NULL)
		return;

	if ((md->flags & MAILDIR_STDIN)) {
		dir = maildir_path(md, NULL);
		if (maildir_opendir(md, dir) == 0) {
			while ((path = maildir_walk(md)))
				(void)unlink(path);
			(void)rmdir(maildir_path(md, NULL));
			(void)rmdir(md->path);
		}
	}

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
		if (path != NULL) {
			log_debug("%s: %s\n", __func__, path);
			return path;
		}

		if (maildir_next(md))
			return NULL;
		path = maildir_path(md, NULL);
		if (maildir_opendir(md, path))
			return NULL;
	}
}

int
maildir_move(const struct maildir *src, const struct maildir *dst,
    struct message *msg, const struct environment *env)
{
	char buf[2][NAME_MAX];
	struct timespec times[2] = {
		{ 0,	UTIME_OMIT },
		{ 0,	0 }
	};
	struct stat st;
	const char *dstname, *flags, *srcname;
	int dstfd, srcfd;
	int doutime = 0;
	int error = 0;

	srcname = pathslice(msg->path, buf[0], -1, -1);
	if (srcname == NULL) {
		warnx("%s: basename not found", msg->path);
		return 1;
	}
	srcfd = dirfd(src->dir);
	if (fstatat(srcfd, srcname, &st, 0) != -1) {
		times[1] = st.st_mtim;
		doutime = 1;
	} else {
		warn("fstatat");
	}

	flags = msgflags(src, dst, msg);
	dstname = maildir_genname(dst, flags, buf[1], sizeof(buf[1]), env);
	dstfd = dirfd(dst->dir);

	if (renameat(srcfd, srcname, dstfd, dstname) == -1) {
		error = 1;
		if (errno == EXDEV) {
			/*
			 * Rename failed since source and destination reside on
			 * different file systems. Fallback to writing a new
			 * message.
			 */
			error = message_writeat(msg, dirfd(dst->dir), dstname);
                        if (error == 0 && (src->flags & MAILDIR_STDIN) == 0)
				error = maildir_unlink(src, msg);
		} else {
			warn("renameat");
		}
	} else if (doutime && utimensat(dstfd, dstname, times, 0) == -1) {
		warn("utimensat");
		error = 1;
	}
	return error;
}

int
maildir_unlink(const struct maildir *md, const struct message *msg)
{
	char buf[NAME_MAX];

	if (pathslice(msg->path, buf, -1, -1) == NULL) {
		warnx("%s: basename not found", msg->path);
		return 1;
	}

	if (unlinkat(dirfd(md->dir), buf, 0) == -1) {
		warn("unlinkat: %s", msg->path);
		return 1;
	}
	return 0;
}

/*
 * Write message to a new file in the given maildir. The destination filename
 * will be written to buf, which must have a capacity of at least NAME_MAX.
 */
int
maildir_write(const struct maildir *src, const struct maildir *dst,
    struct message *msg, char *buf, size_t bufsiz,
    const struct environment *env)
{
	const char *flags;

	flags = msgflags(dst, src, msg);
	maildir_genname(dst, flags, buf, bufsiz, env);

	return message_writeat(msg, dirfd(dst->dir), buf);
}

static int
maildir_next(struct maildir *md)
{
	if ((md->flags & MAILDIR_STDIN))
		return 1;

	switch (md->subdir) {
	case SUBDIR_NEW:
		md->subdir = SUBDIR_CUR;
		return 0;
	case SUBDIR_CUR:
		break;
	}
	return 1;
}

static int
maildir_opendir(struct maildir *md, const char *path)
{
	if (md->dir != NULL)
		closedir(md->dir);

	log_debug("%s: %s\n", __func__, path);

	md->dir = opendir(path);
	if (md->dir == NULL) {
		warn("opendir: %s", path);
		return 1;
	}
	return 0;
}

static char *
maildir_genname(const struct maildir *dst, const char *flags,
    char *buf, size_t bufsiz, const struct environment *env)
{
	long long ts;
	pid_t pid;
	int fd, n;
	int count;

	ts = time(NULL);
	pid = getpid();
	count = arc4random() % 128;
	for (;;) {
		count++;
		n = snprintf(buf, bufsiz, "%lld.%d_%d.%s%s",
		    ts, pid, count, env->hostname, flags);
		if (n < 0 || n >= NAME_MAX)
			errc(1, ENAMETOOLONG, "%s", __func__);
		fd = openat(dirfd(dst->dir), buf, O_WRONLY | O_CREAT | O_EXCL,
		    S_IRUSR | S_IWUSR);
		if (fd == -1) {
			if (errno == EEXIST) {
				log_debug("%s: %s: file exists\n",
				    __func__, buf);
				continue;
			}
			err(1, "openat: %s", buf);
		}
		close(fd);
		return buf;
	}
}

static const char *
maildir_path(struct maildir *md, const char *filename)
{
	const char *dirname = NULL;

	switch (md->subdir) {
	case SUBDIR_NEW:
		dirname = "new";
		break;
	case SUBDIR_CUR:
		dirname = "cur";
		break;
	}
	return pathjoin(md->buf, md->path, dirname, filename);
}

static int
maildir_stdin(struct maildir *md, const struct environment *env)
{
	char buf[BUFSIZ], name[NAME_MAX];
	const char *path;
	ssize_t nr, nw;
	int fd;
	int error = 0;

	md->path = malloc(PATH_MAX);
	if (md->path == NULL)
		err(1, NULL);
	pathjoin(md->path, env->tmpdir, "mdsort-XXXXXXXX", NULL);
	if (mkdtemp(md->path) == NULL) {
		warn("mkdtemp");
		return 1;
	}

	path = maildir_path(md, NULL);
	if (mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR) == -1) {
		warn("mkdir");
		return 1;
	}
	if (maildir_opendir(md, path))
		return 1;

	maildir_genname(md, "", name, sizeof(name), env);
	fd = openat(dirfd(md->dir), name, O_WRONLY | O_EXCL);
	if (fd == -1) {
		warn("openat: %s/%s", path, name);
		return 1;
	}
	for (;;) {
		nr = read(STDIN_FILENO, buf, sizeof(buf));
		if (nr == -1) {
			error = 1;
			warn("read");
			break;
		} else if (nr == 0) {
			break;
		}

		nw = write(fd, buf, nr);
		if (nw == -1) {
			error = 1;
			warn("write: %s/%s", path, name);
			break;
		}
	}
	close(fd);

	return error;
}

static const char *
maildir_read(struct maildir *md)
{
	struct dirent *ent;

	for (;;) {
		ent = readdir(md->dir);
		if (ent == NULL)
			return 0;
		if (ent->d_type != DT_REG)
			continue;

		return maildir_path(md, ent->d_name);
	}
}

static const char *
msgflags(const struct maildir *src, const struct maildir *dst,
    struct message *msg)
{
	if (src->subdir == SUBDIR_NEW && dst->subdir == SUBDIR_CUR)
		message_set_flags(msg, 'S', 1);
	else if (src->subdir == SUBDIR_CUR && dst->subdir == SUBDIR_NEW)
		message_set_flags(msg, 'S', 0);

	return message_get_flags(msg);
}

static int
parsesubdir(const char *path, enum subdir *subdir)
{
	char buf[NAME_MAX];

	if (pathslice(path, buf, -1, -1) == NULL) {
		return 1;
	} else if (strcmp(buf, "new") == 0) {
		*subdir = SUBDIR_NEW;
		return 0;
	} else if (strcmp(buf, "cur") == 0) {
		*subdir = SUBDIR_CUR;
		return 0;
	}
	warnx("%s: subdir not found", path);
	return 1;
}
