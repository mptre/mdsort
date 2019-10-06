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

#define FLAGS_MAX	64

static int maildir_fd(const struct maildir *);
static int maildir_genname(const struct maildir *, const char *,
    char *, size_t, const struct environment *);
static const char *maildir_next(struct maildir *);
static int maildir_opendir(struct maildir *, const char *);
static int maildir_stdin(struct maildir *, const struct environment *);
static const char *maildir_path(struct maildir *);
static int maildir_read(struct maildir *, struct maildir_entry *);

static int isfile(int, const char *);
static int msgflags(const struct maildir *, const struct maildir *,
    const struct message *, char *, size_t);
static int parsesubdir(const char *, enum subdir *);

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
		siz = sizeof(md->md_path);
		if (strlcpy(md->md_path, path, siz) >= siz) {
			warnc(ENAMETOOLONG, "%s", __func__);
			goto err;
		}
	} else {
		if (parsesubdir(path, &md->md_subdir))
			goto err;

		siz = sizeof(md->md_path);
		if (pathslice(path, md->md_path, siz, 0, -1) == NULL)
			goto err;
	}
	path = maildir_path(md);
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
	struct maildir_entry me;
	const char *dir;

	if (md == NULL)
		return;

	if (md->md_flags & MAILDIR_STDIN) {
		/* Best effort removal of the temporary maildir. */
		dir = maildir_path(md);
		if (maildir_opendir(md, dir) == 0) {
			while (maildir_walk(md, &me) == 1)
				(void)unlinkat(me.e_dirfd, me.e_path, 0);
			(void)rmdir(dir);
			(void)rmdir(md->md_path);
		}
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
 *
 *     0    All files have been traversed.
 *
 *     -1   An error occurred.
 */
int
maildir_walk(struct maildir *md, struct maildir_entry *me)
{
	const char *path;
	int r;

	if ((md->md_flags & MAILDIR_WALK) == 0)
		return 0;

	for (;;) {
		if ((r = maildir_read(md, me)))
			return r;

		path = maildir_next(md);
		if (path == NULL)
			return 0;
		if (maildir_opendir(md, path))
			return 0;
	}
}

/*
 * Move the message located in src to dst. The destination filename will be
 * written to buf, which must have a capacity of at least NAME_MAX plus one.
 * Returns zero on success, non-zero otherwise.
 */
int
maildir_move(const struct maildir *src, const struct maildir *dst,
    struct message *msg, char *buf, size_t bufsiz,
    const struct environment *env)
{
	char flags[FLAGS_MAX], sbuf[NAME_MAX + 1];
	struct timespec times[2] = {
		{ 0,	UTIME_OMIT },
		{ 0,	0 }
	};
	struct stat st;
	const char *dstname, *srcname;
	int dstfd, fd, srcfd;
	int doutime = 0;
	int error = 0;

	srcname = pathslice(msg->me_path, sbuf, sizeof(sbuf), -1, -1);
	if (srcname == NULL) {
		warnx("%s: basename not found", msg->me_path);
		return 1;
	}
	srcfd = maildir_fd(src);
	if (fstatat(srcfd, srcname, &st, 0) != -1) {
		times[1] = st.st_mtim;
		doutime = 1;
	} else {
		warn("fstatat");
	}

	if (msgflags(src, dst, msg, flags, sizeof(flags)))
		return 1;
	fd = maildir_genname(dst, flags, buf, bufsiz, env);
	if (fd == -1)
		return 1;
	dstname = buf;
	dstfd = maildir_fd(dst);

	if (renameat(srcfd, srcname, dstfd, dstname) == -1) {
		error = 1;
		if (errno == EXDEV) {
			/*
			 * Rename failed since source and destination reside on
			 * different file systems. Fallback to writing a new
			 * message.
			 */
			error = message_writeat(msg, fd,
			    src->md_flags & MAILDIR_SYNC);
			fd = -1;
			if (error)
				(void)unlinkat(dstfd, dstname, 0);
			else
				error = maildir_unlink(src, msg);
		} else {
			warn("renameat");
		}
	} else if (doutime && utimensat(dstfd, dstname, times, 0) == -1) {
		warn("utimensat");
		error = 1;
	}
	if (fd != -1)
		close(fd);

	return error;
}

/*
 * Remove the message located in the given maildir.
 * Returns zero on success, non-zero otherwise.
 */
int
maildir_unlink(const struct maildir *md, const struct message *msg)
{
	char buf[NAME_MAX + 1];

	if (pathslice(msg->me_path, buf, sizeof(buf), -1, -1) == NULL) {
		warnx("%s: basename not found", msg->me_path);
		return 1;
	}

	if (unlinkat(maildir_fd(md), buf, 0) == -1) {
		warn("unlinkat: %s", msg->me_path);
		return 1;
	}
	return 0;
}

/*
 * Write message to a new file in the given maildir. The destination filename
 * will be written to buf, which must have a capacity of at least NAME_MAX plus
 * one.
 */
int
maildir_write(const struct maildir *src, const struct maildir *dst,
    struct message *msg, char *buf, size_t bufsiz,
    const struct environment *env)
{
	char flags[FLAGS_MAX];
	int error, fd;

	if (msgflags(src, dst, msg, flags, sizeof(flags)))
		return 1;
	fd = maildir_genname(dst, flags, buf, bufsiz, env);
	if (fd == -1)
		return 1;

	error = message_writeat(msg, fd, src->md_flags & MAILDIR_SYNC);
	if (error)
		(void)unlinkat(maildir_fd(dst), buf, 0);

	return error;
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

	return maildir_path(md);
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
 * Create a new file rooted in the given maildir.
 * Returns a write-only file descriptor to the newly created file.
 * Otherwise, -1 is returned.
 */
static int
maildir_genname(const struct maildir *dst, const char *flags,
    char *buf, size_t bufsiz, const struct environment *env)
{
	long long ts;
	int fd, n;
	int count;

	ts = env->ev_now;
	count = arc4random() % 128;
	for (;;) {
		count++;
		n = snprintf(buf, bufsiz, "%lld.%d_%d.%s%s",
		    ts, env->ev_pid, count, env->ev_hostname, flags);
		if (n < 0 || (size_t)n >= bufsiz) {
			warnc(ENAMETOOLONG, "%s", __func__);
			return -1;
		}
		fd = openat(maildir_fd(dst), buf, O_WRONLY | O_CREAT | O_EXCL,
		    S_IRUSR | S_IWUSR);
		if (fd == -1) {
			if (errno == EEXIST) {
				log_debug("%s: %s: file exists\n",
				    __func__, buf);
				continue;
			}
			warn("openat: %s", buf);
			return -1;
		}
		return fd;
	}
}

static const char *
maildir_path(struct maildir *md)
{
	const char *path;
	const char *subdir = NULL;

	switch (md->md_subdir) {
	case SUBDIR_NEW:
		subdir = "new";
		break;
	case SUBDIR_CUR:
		subdir = "cur";
		break;
	}
	path = pathjoin(md->md_buf, sizeof(md->md_buf), md->md_path, subdir);
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
	int fd;
	int error = 0;

	if (pathjoin(md->md_path, sizeof(md->md_path), env->ev_tmpdir,
		    "mdsort-XXXXXXXX") == NULL) {
		warnc(ENAMETOOLONG, "%s", __func__);
		return 1;
	}
	if (mkdtemp(md->md_path) == NULL) {
		warn("mkdtemp");
		return 1;
	}

	path = maildir_path(md);
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
	fd = maildir_genname(md, "", name, sizeof(name), env);
	if (fd == -1)
		return 1;

	for (;;) {
		nr = read(STDIN_FILENO, buf, sizeof(buf));
		if (nr == -1) {
			error = 1;
			warn("read");
			break;
		}
		if (nr == 0)
			break;

		nw = write(fd, buf, nr);
		if (nw == -1) {
			error = 1;
			warn("write: %s/%s", path, name);
			break;
		}
	}
	if (error) {
		close(fd);
		return error;
	}

	if ((md->md_flags & MAILDIR_SYNC) && fsync(fd) == -1) {
		warn("fsync");
		error = 1;
	}
	close(fd);

	return error;
}

static int
maildir_read(struct maildir *md, struct maildir_entry *me)
{
	const struct dirent *ent;

	for (;;) {
		/*
		 * Necessary to reset errno in order to distinguish between
		 * reaching end of directory and errors.
		 */
		errno = 0;
		ent = readdir(md->md_dir);
		if (ent == NULL) {
			if (errno) {
				warn("readdir");
				return -1;
			}
			return 0;
		}
		switch (ent->d_type) {
		case DT_UNKNOWN:
			/*
			 * Some filesystems like XFS does not return the file
			 * type and stat(2) must instead be used.
			 */
			if (!isfile(maildir_fd(md), ent->d_name))
				continue;
			break;
		case DT_REG:
			break;
		default:
			continue;
		}

		log_debug("%s: %s/%s\n", __func__, md->md_buf, ent->d_name);
		me->e_dir = md->md_buf;
		me->e_dirfd = maildir_fd(md);
		me->e_path = ent->d_name;
		return 1;
	}
}

static int
isfile(int dirfd, const char *path)
{
	struct stat sb;

	if (fstatat(dirfd, path, &sb, AT_SYMLINK_NOFOLLOW) == -1)
		return 0;

	return (sb.st_mode & S_IFMT) == S_IFREG;
}

static int
msgflags(const struct maildir *src, const struct maildir *dst,
    const struct message *msg, char *buf, size_t bufsiz)
{
	struct message_flags flags = msg->me_flags;

	if (src->md_subdir == SUBDIR_NEW && dst->md_subdir == SUBDIR_CUR) {
		if (message_flags_set(&flags, 'S', 1))
			return 1;
	} else if (src->md_subdir == SUBDIR_CUR &&
	    dst->md_subdir == SUBDIR_NEW) {
		if (message_flags_set(&flags, 'S', 0))
			return 1;
	}

	if (message_flags_str(&flags, buf, bufsiz) == NULL)
		return 1;

	return 0;
}

static int
parsesubdir(const char *path, enum subdir *subdir)
{
	char buf[NAME_MAX + 1];

	if (pathslice(path, buf, sizeof(buf), -1, -1) == NULL) {
		/* nothing */;
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
