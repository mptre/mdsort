#include "config.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"
#include "message.h"

#define FLAGS_MAX	64

enum subdir {
	SUBDIR_NEW,
	SUBDIR_CUR,
};

struct maildir {
	char		 md_root[PATH_MAX];	/* root directory */
	char		 md_path[PATH_MAX];	/* current directory */
	DIR		*md_dir;
	enum subdir	 md_subdir;
	unsigned int	 md_flags;
};

static int		 maildir_fd(const struct maildir *);
static int		 maildir_genname(const struct maildir *, const char *,
    char *, size_t, const struct environment *);
static const char	*maildir_next(struct maildir *);
static int		 maildir_opendir(struct maildir *, const char *);
static int		 maildir_stdin(struct maildir *,
    const struct environment *);
static const char	*maildir_set_path(struct maildir *);
static int		 maildir_read(struct maildir *, struct maildir_entry *);
static int		 maildir_rename(const struct maildir *,
    const struct maildir *, const char *, const char *);

static int	isfile(int, const char *);
static int	msgflags(const struct maildir *, const struct maildir *,
    const struct message *, char *, size_t);
static int	parsesubdir(const char *, enum subdir *);

/*
 * Open the maildir directory located at path.
 *
 * The flags may be any combination of the following values:
 *
 *     MAILDIR_WALK     Invoking maildir_walk() will traverse all messages
 *                      present in the cur and new subdirectories rooted at
 *                      path.
 *
 *     MAILDIR_STDIN    Read messages from stdin.
 *
 * The caller is responsible for freeing the returned memory using
 * maildir_close().
 */
struct maildir *
maildir_open(const char *path, unsigned int flags,
    const struct environment *env)
{
	struct maildir *md;
	size_t siz;

	md = calloc(1, sizeof(*md));
	if (md == NULL)
		err(1, NULL);
	md->md_subdir = SUBDIR_NEW;
	md->md_flags = flags;

	if (md->md_flags & MAILDIR_STDIN) {
		if (maildir_stdin(md, env))
			goto err;

		return md;
	}

	if (md->md_flags & MAILDIR_WALK) {
		siz = sizeof(md->md_root);
		if (strlcpy(md->md_root, path, siz) >= siz) {
			warnc(ENAMETOOLONG, "%s", __func__);
			goto err;
		}
	} else {
		if (parsesubdir(path, &md->md_subdir))
			goto err;

		siz = sizeof(md->md_root);
		if (pathslice(path, md->md_root, siz, 0, -1) == NULL)
			goto err;
	}
	path = maildir_set_path(md);
	if (maildir_opendir(md, path))
		goto err;

	return md;

err:
	maildir_close(md);
	return NULL;
}

void
maildir_close(struct maildir *md)
{
	if (md == NULL)
		return;

	if (md->md_flags & MAILDIR_STDIN) {
		struct maildir_entry me;

		/* Best effort removal of the temporary maildir. */
		rewinddir(md->md_dir);
		while (maildir_walk(md, &me) == 1)
			(void)unlinkat(me.e_dirfd, me.e_path, 0);
		(void)rmdir(md->md_path);
		(void)rmdir(md->md_root);
	}

	if (md->md_dir != NULL)
		closedir(md->md_dir);
	free(md);
}

/*
 * Traverse the given maildir. Returns one of the following:
 *
 *     1    A new file was encountered and the maildir entry is populated with
 *          the details.
 *     0    All files have been traversed.
 *    -1    An error occurred.
 */
int
maildir_walk(struct maildir *md, struct maildir_entry *me)
{
	if ((md->md_flags & MAILDIR_WALK) == 0)
		return 0;

	for (;;) {
		const char *path;
		int r;

		if ((r = maildir_read(md, me)))
			return r;

		path = maildir_next(md);
		if (path == NULL)
			return 0;
		if (maildir_opendir(md, path))
			return -1;
	}
}

/*
 * Move the message located in src to dst. The message path will be updated
 * accordingly. Returns zero on success, non-zero otherwise.
 */
int
maildir_move(struct maildir *src, const struct maildir *dst,
    struct message *msg, const struct environment *env)
{
	char dstname[NAME_MAX + 1], flags[FLAGS_MAX];
	struct timespec times[2] = {
		{ 0,	UTIME_OMIT },
		{ 0,	0 }
	};
	struct stat sb;
	const char *srcname;
	int doutime = 0;
	int error = 0;
	int dstfd, fd, srcfd;

	srcname = message_get_name(msg);
	srcfd = maildir_fd(src);

	if ((src->md_flags & MAILDIR_STDIN) &&
	    strcmp(src->md_root, dst->md_root) == 0) {
		warnx("cannot move message to temporary directory");
		return 1;
	}

	if ((src->md_flags & MAILDIR_STDIN) == 0) {
		if (fstatat(srcfd, srcname, &sb, 0) != -1) {
			times[1] = sb.st_mtim;
			doutime = 1;
		} else {
			warn("fstatat");
		}
	}

	if (msgflags(src, dst, msg, flags, sizeof(flags)))
		return 1;
	fd = maildir_genname(dst, flags, dstname, sizeof(dstname), env);
	if (fd == -1)
		return 1;
	dstfd = maildir_fd(dst);

	error = maildir_rename(src, dst, srcname, dstname);
	if (error && errno == EXDEV) {
		/*
		 * Rename failed since source and destination reside on
		 * different file systems. Fallback to writing a new message.
		 */
		error = message_write(msg, fd);
		if (!error)
			error = maildir_unlink(src, srcname);
	}
	/*
	 * Try to reduce side effects by removing the new message in
	 * case of failure(s).
	 */
	if (error)
		(void)maildir_unlink(dst, dstname);

	close(fd);

	if (!error && doutime && utimensat(dstfd, dstname, times, 0) == -1) {
		warn("utimensat");
		error = 1;
	}

	if (!error)
		error = message_set_file(msg, dst->md_path, dstname, -1);

	return error;
}

/*
 * Remove the path located in the given maildir. Returns zero on success,
 * non-zero otherwise.
 */
int
maildir_unlink(const struct maildir *md, const char *path)
{
	if (FAULT("maildir_unlink"))
		return 1;

	if (unlinkat(maildir_fd(md), path, 0) == -1) {
		warn("unlinkat: %s/%s", md->md_path, path);
		return 1;
	}
	return 0;
}

/*
 * Write message to a new file in the given maildir and remove the old file,
 * assuming the write succeeded.
 */
int
maildir_write(struct maildir *md, struct message *msg,
    const struct environment *env)
{
	char flags[FLAGS_MAX], name[NAME_MAX + 1];
	int error, fd, rdfd;

	if (msgflags(md, md, msg, flags, sizeof(flags)))
		return 1;
	fd = maildir_genname(md, flags, name, sizeof(name), env);
	if (fd == -1)
		return 1;

	error = message_write(msg, fd);
	close(fd);
	if (!error)
		error = maildir_unlink(md, message_get_name(msg));
	/*
	 * Either writing the new message or removing the old one failed, try to
	 * reduce side effects by removing the new message.
	 */
	if (error) {
		(void)maildir_unlink(md, name);
		return error;
	}

	/*
	 * Update the message fd as the newly written message might have been
	 * modified due to addition of headers etc. This is of importance to let
	 * any following action(s) operating on the same message to observe the
	 * modifications. Note, a message fd must be readable as opposed of the
	 * one from maildir_genname() which is only writeable.
	 */
	rdfd = openat(maildir_fd(md), name, O_RDONLY | O_CLOEXEC);
	if (rdfd == -1) {
		warn("openat: %s/%s", md->md_path, name);
		return 1;
	}
	error = message_set_file(msg, md->md_path, name, rdfd);
	if (error)
		close(rdfd);

	return error;
}

/*
 * Returns 0 if the two given maildirs are equal.
 */
int
maildir_cmp(const struct maildir *md1, const struct maildir *md2)
{
	if (md1->md_subdir > md2->md_subdir)
		return 1;
	if (md1->md_subdir < md2->md_subdir)
		return -1;
	return strcmp(md1->md_root, md2->md_root);
}

static const char *
maildir_next(struct maildir *md)
{
	if (md->md_flags & MAILDIR_STDIN)
		return NULL;

	switch (md->md_subdir) {
	case SUBDIR_NEW:
		md->md_subdir = SUBDIR_CUR;
		break;
	case SUBDIR_CUR:
		return NULL;
	}

	return maildir_set_path(md);
}

static int
maildir_opendir(struct maildir *md, const char *path)
{
	if (md->md_dir != NULL)
		closedir(md->md_dir);

	log_debug("%s: %s\n", __func__, path);

	md->md_dir = opendir(path);
	if (md->md_dir == NULL) {
		warn("opendir: %s", path);
		return 1;
	}
	return 0;
}

static int
maildir_fd(const struct maildir *md)
{
	return dirfd(md->md_dir);
}

/*
 * Create a new file rooted in the given maildir. Returns a write-only file
 * descriptor to the newly created file. Otherwise, -1 is returned.
 */
static int
maildir_genname(const struct maildir *md, const char *flags, char *buf,
    size_t bufsiz, const struct environment *env)
{
	long long ts;
	unsigned int count;

	ts = env->ev_now;
	count = arc4random() % 128;
	for (;;) {
		int fd, n;

		count++;
		n = snprintf(buf, bufsiz, "%lld.%d_%u.%s%s",
		    ts, env->ev_pid, count, env->ev_hostname,
		    flags ? flags : "");
		if (n < 0 || (size_t)n >= bufsiz) {
			warnc(ENAMETOOLONG, "%s", __func__);
			return -1;
		}
		fd = openat(maildir_fd(md), buf,
		    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR);
		if (fd == -1) {
			if (errno == EEXIST) {
				log_debug("%s: %s: file exists\n",
				    __func__, buf);
				continue;
			}
			warn("openat: %s/%s", md->md_path, buf);
			return -1;
		}
		return fd;
	}
}

static const char *
maildir_set_path(struct maildir *md)
{
	const char *subdir = NULL;
	const char *path;

	switch (md->md_subdir) {
	case SUBDIR_NEW:
		subdir = "new";
		break;
	case SUBDIR_CUR:
		subdir = "cur";
		break;
	}
	path = pathjoin(md->md_path, sizeof(md->md_path), md->md_root, subdir);
	if (path == NULL)
		errc(1, ENAMETOOLONG, "%s", __func__);
	return path;
}

static int
maildir_stdin(struct maildir *md, const struct environment *env)
{
	char buf[BUFSIZ], name[NAME_MAX + 1];
	const char *path;
	ssize_t nr, nw;
	int error = 0;
	int fd;

	if (pathjoin(md->md_root, sizeof(md->md_root), env->ev_tmpdir,
	    "mdsort-XXXXXXXX") == NULL) {
		warnc(ENAMETOOLONG, "%s", __func__);
		return 1;
	}
	if (mkdtemp(md->md_root) == NULL) {
		warn("mkdtemp");
		return 1;
	}

	path = maildir_set_path(md);
	if (mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR) == -1) {
		warn("mkdir");
		return 1;
	}
	if (maildir_opendir(md, path))
		return 1;

	/*
	 * No need to remove the created file in case of an error since
	 * maildir_close() removes the complete temporary directory.
	 */
	fd = maildir_genname(md, NULL, name, sizeof(name), env);
	if (fd == -1)
		return 1;

	for (;;) {
		nr = read(STDIN_FILENO, buf, sizeof(buf));
		if (nr == -1) {
			warn("read");
			error = 1;
			goto out;
		}
		if (nr == 0)
			break;

		nw = write(fd, buf, (size_t)nr);
		if (nw == -1) {
			warn("write: %s/%s", path, name);
			error = 1;
			goto out;
		}
	}

	if (fsync(fd) == -1) {
		warn("fsync");
		error = 1;
	}

out:
	/* Don't ignore potential EIO errors. */
	if (close(fd) == -1) {
		warn("close");
		error = 1;
	}
	return error;
}

static int
maildir_read(struct maildir *md, struct maildir_entry *me)
{
	const struct dirent *ent;
	unsigned int type;

	if (FAULT("maildir_read"))
		return -1;

	for (;;) {
		/*
		 * Necessary to reset errno in order to distinguish between
		 * reaching end of directory and errors.
		 */
		errno = 0;
		ent = readdir(md->md_dir);
		if (ent == NULL) {
			if (errno) {
				warn("readdir: %s", md->md_path);
				return -1;
			}
			return 0;
		}

		type = ent->d_type;
		if (FAULT("readdir_type"))
			type = DT_UNKNOWN;

		switch (type) {
		case DT_UNKNOWN:
			/*
			 * Some filesystems like XFS does not return the file
			 * type and stat(2) must instead be used.
			 */
			if (!isfile(maildir_fd(md), ent->d_name))
				goto unknown;
			break;
		case DT_DIR:
			continue;
		case DT_REG:
			break;
		default:
unknown:
			log_debug("%s: %s/%s: unknown file type %u\n",
			    __func__, md->md_path, ent->d_name, type);
			continue;
		}

		log_debug("%s: %s/%s\n", __func__, md->md_path, ent->d_name);
		me->e_dir = md->md_path;
		me->e_dirfd = maildir_fd(md);
		me->e_path = ent->d_name;
		return 1;
	}
}

static int
maildir_rename(const struct maildir *src, const struct maildir *dst,
    const char *srcname, const char *dstname)
{
	if (FAULT("maildir_rename"))
		return 1;

	if (renameat(maildir_fd(src), srcname,
	    maildir_fd(dst), dstname) == -1) {
		/* Silence as we're about to recover. */
		if (errno != EXDEV)
			warn("renameat");
		return 1;
	}
	return 0;
}

static int
isfile(int dirfd, const char *path)
{
	struct stat sb;

	/* Best effort, ignore errors. */
	if (fstatat(dirfd, path, &sb, AT_SYMLINK_NOFOLLOW) == -1)
		return 0;
	return S_ISREG(sb.st_mode);
}

static int
msgflags(const struct maildir *src, const struct maildir *dst,
    const struct message *msg, char *buf, size_t bufsiz)
{
	struct message_flags flags = *message_get_flags(msg);

	if (src->md_subdir == SUBDIR_NEW && dst->md_subdir == SUBDIR_CUR) {
		if (message_flags_set(&flags, 'S'))
			return 1;
	} else if (src->md_subdir == SUBDIR_CUR &&
	    dst->md_subdir == SUBDIR_NEW) {
		if (message_flags_clr(&flags, 'S'))
			return 1;
	}
	return message_flags_str(&flags, buf, bufsiz) == NULL ? 1 : 0;
}

static int
parsesubdir(const char *path, enum subdir *subdir)
{
	char buf[NAME_MAX + 1];

	if (pathslice(path, buf, sizeof(buf), -1, -1) == NULL) {
		/* nothing */
	} else if (strcmp(buf, "new") == 0) {
		*subdir = SUBDIR_NEW;
		return 0;
	} else if (strcmp(buf, "cur") == 0) {
		*subdir = SUBDIR_CUR;
		return 0;
	}
	warnx("%s: %s: subdir not found", __func__, path);
	return 1;
}
