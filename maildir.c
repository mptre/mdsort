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
static int maildir_rename(const struct maildir *, const struct maildir *,
    const char *, const char *);

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

	if (md == NULL)
		return;

	if (md->md_flags & MAILDIR_STDIN) {
		struct maildir_entry me;

		/* Best effort removal of the temporary maildir. */
		rewinddir(md->md_dir);
		while (maildir_walk(md, &me) == 1)
			(void)unlinkat(me.e_dirfd, me.e_path, 0);
		(void)rmdir(maildir_path(md));
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
			return -1;
	}
}

/*
 * Move the message located in src to dst. The destination filename will be
 * written to buf, which must have a capacity of at least NAME_MAX plus one.
 * Returns zero on success, non-zero otherwise.
 */
int
maildir_move(struct maildir *src, const struct maildir *dst,
    struct message *msg, char *buf, size_t bufsiz,
    const struct environment *env)
{
	char flags[FLAGS_MAX];
	struct timespec times[2] = {
		{ 0,	UTIME_OMIT },
		{ 0,	0 }
	};
	struct stat sb;
	const char *dstname, *srcname;
	int dstfd, fd, srcfd;
	int doutime = 0;
	int error = 0;

	srcname = msg->me_name;
	srcfd = maildir_fd(src);

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
	fd = maildir_genname(dst, flags, buf, bufsiz, env);
	if (fd == -1)
		return 1;
	dstname = buf;
	dstfd = maildir_fd(dst);

	if (maildir_rename(src, dst, srcname, dstname)) {
		if (errno == EXDEV) {
			/*
			 * Rename failed since source and destination reside on
			 * different file systems. Fallback to writing a new
			 * message.
			 */
			error = message_write(msg, fd);
			if (error == 0)
				error = maildir_unlink(src, srcname);
			if (error) {
				/*
				 * Either writing the new message or removing
				 * the old one failed, try to reduce side
				 * effects by removing the new message.
				 */
				(void)maildir_unlink(dst, dstname);
			}
		} else {
			warn("renameat");
			error = 1;
		}
	}

	close(fd);

	if (error == 0 && doutime &&
	    utimensat(dstfd, dstname, times, 0) == -1) {
		warn("utimensat");
		error = 1;
	}

	return error;
}

/*
 * Remove the path located in the given maildir.
 * Returns zero on success, non-zero otherwise.
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
 * Write message to a new file in the given maildir. The destination filename
 * will be written to buf, which must have a capacity of at least NAME_MAX plus
 * one.
 */
int
maildir_write(struct maildir *src, const struct maildir *dst,
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

	error = message_write(msg, fd);
	close(fd);
	if (error)
		(void)maildir_unlink(dst, buf);

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
	unsigned int count;
	int fd, n;

	ts = env->ev_now;
	count = arc4random() % 128;
	for (;;) {
		count++;
		n = snprintf(buf, bufsiz, "%lld.%d_%u.%s%s",
		    ts, env->ev_pid, count, env->ev_hostname,
		    flags ? flags : "");
		if (n < 0 || (size_t)n >= bufsiz) {
			warnc(ENAMETOOLONG, "%s", __func__);
			return -1;
		}
		fd = openat(maildir_fd(dst), buf,
		    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR);
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
	int fd;
	int error = 0;

	if (pathjoin(md->md_root, sizeof(md->md_root), env->ev_tmpdir,
		    "mdsort-XXXXXXXX") == NULL) {
		warnc(ENAMETOOLONG, "%s", __func__);
		return 1;
	}
	if (mkdtemp(md->md_root) == NULL) {
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
	fd = maildir_genname(md, NULL, name, sizeof(name), env);
	if (fd == -1)
		return 1;

	for (;;) {
		nr = read(STDIN_FILENO, buf, sizeof(buf));
		if (nr == -1) {
			error = 1;
			warn("read");
			goto out;
		}
		if (nr == 0)
			break;

		nw = write(fd, buf, nr);
		if (nw == -1) {
			error = 1;
			warn("write: %s/%s", path, name);
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

	if (renameat(maildir_fd(src), srcname, maildir_fd(dst), dstname) == -1)
		return 1;
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
	struct message_flags flags = msg->me_mflags;

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
