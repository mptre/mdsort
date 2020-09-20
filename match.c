#include "config.h"

#include <sys/wait.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

static const struct match *matches_find_interpolate(const struct match_list *);
static void matches_merge(struct match_list *, struct match *);

static const char *match_get(const struct match *, unsigned int);

static int isbackref(const char *, unsigned int *);
static int exec(char *const *, int);
static int interpolate(const struct match *, const char *, char **);

/*
 * Append the given match to the list and construct the maildir destination path
 * if needed.
 */
int
matches_append(struct match_list *ml, struct match *mh,
    const struct message *msg)
{
	const char *p;
	size_t siz;

	matches_merge(ml, mh);
	TAILQ_INSERT_TAIL(ml, mh, mh_entry);

	if ((mh->mh_expr->ex_flags & EXPR_FLAG_PATH) == 0)
		return 0;

	if (mh->mh_maildir[0] == '\0') {
		/* Infer maildir from message path. */
		siz = sizeof(mh->mh_maildir);
		p = pathslice(msg->me_path, mh->mh_maildir, siz, 0, -2);
		if (p == NULL) {
			warnx("%s: %s: maildir not found",
			    __func__, msg->me_path);
			return 1;
		}
	}
	if (mh->mh_subdir[0] == '\0') {
		/* Infer subdir from message path. */
		siz = sizeof(mh->mh_subdir);
		p = pathslice(msg->me_path, mh->mh_subdir, siz, -2, -2);
		if (p == NULL) {
			warnx("%s: %s: subdir not found",
			    __func__, msg->me_path);
			return 1;
		}
	}

	siz = sizeof(mh->mh_path);
	if (pathjoin(mh->mh_path, siz, mh->mh_maildir, mh->mh_subdir) == NULL) {
		warnc(ENAMETOOLONG, "%s", __func__);
		return 1;
	}

	return 0;
}

void
matches_clear(struct match_list *ml)
{
	struct match *mh;

	while ((mh = TAILQ_FIRST(ml)) != NULL) {
		match_reset(mh);
		TAILQ_REMOVE(ml, mh, mh_entry);
	}

}

int
matches_interpolate(struct match_list *ml, struct message *msg)
{
	const struct match *mi;
	struct match *mh;

	/*
	 * Note that mi might be NULL but it's not considered an error as long
	 * as the string to interpolate is missing back-references.
	 */
	mi = matches_find_interpolate(ml);

	TAILQ_FOREACH(mh, ml, mh_entry) {
		switch (mh->mh_expr->ex_type) {
		case EXPR_TYPE_MOVE:
		case EXPR_TYPE_FLAG: {
			char *path = NULL;
			size_t n, siz;

			if (interpolate(mi, mh->mh_path, &path))
				return 1;
			siz = sizeof(mh->mh_path);
			n = strlcpy(mh->mh_path, path, siz);
			if (n >= siz)
				warnx("%s: interpolated string too long", path);
			free(path);
			if (n >= siz)
				return 1;
			break;
		}

		case EXPR_TYPE_LABEL: {
			const char *str;
			char *label = NULL;

			str = message_get_header1(msg, "X-Label");
			if (str == NULL) {
				/*
				 * This should never happen since a label action
				 * always sets the X-Label header in
				 * expr_eval_label(). But some static analysis
				 * tools interpret usage of str below as a
				 * potential NULL deference.
				 */
				return 1;
			}
			if (interpolate(mi, str, &label)) {
				free(label);
				return 1;
			}
			message_set_header(msg, "X-Label", label);
			break;
		}

		case EXPR_TYPE_EXEC: {
			const struct string *str;
			size_t len;
			size_t nargs = 0;

			/* Make room for NULL-terminator. */
			len = strings_len(mh->mh_expr->ex_strings) + 1;
			mh->mh_exec = reallocarray(NULL, len, sizeof(char *));
			if (mh->mh_exec == NULL)
				err(1, NULL);
			memset(mh->mh_exec, 0, len * sizeof(char *));
			mh->mh_nexec = len;
			TAILQ_FOREACH(str, mh->mh_expr->ex_strings, entry) {
				char *arg = NULL;

				if (interpolate(mi, str->val, &arg)) {
					free(arg);
					return 1;
				}
				mh->mh_exec[nargs++] = arg;
			}
			break;
		}

		default:
			continue;
		}
	}

	return 0;
}

int
matches_exec(const struct match_list *ml, struct maildir *src,
    struct message *msg, int *reject, const struct environment *env)
{
	char path[NAME_MAX + 1], tmp[NAME_MAX + 1];
	struct maildir *dst = NULL;
	struct match *mh;
	const char *path_save;
	int error = 0;

	path_save = msg->me_path;

	TAILQ_FOREACH(mh, ml, mh_entry) {
		switch (mh->mh_expr->ex_type) {
		case EXPR_TYPE_FLAG:
		case EXPR_TYPE_MOVE:
			/*
			 * Move message and update the source maildir and
			 * message path, assuming the destination maildir is
			 * different from the source maildir. This is of
			 * importance if a following action requires a source
			 * maildir.
			 */
			maildir_close(dst);
			dst = maildir_open(mh->mh_path, 0, env);
			if (dst == NULL) {
				error = 1;
				break;
			}
			if (maildir_move(src, dst, msg,
				    tmp, sizeof(tmp), env)) {
				error = 1;
				break;
			}

			(void)strlcpy(path, tmp, sizeof(path));
			msg->me_path = path;

			/*
			 * The following logic might look fragile as the
			 * destination maildir is freed above. However, a match
			 * list can only contain one flag or move action as
			 * enforced by matches_merge().
			 */
			if (maildir_cmp(src, dst))
				src = dst;
			break;

		case EXPR_TYPE_DISCARD:
			if (maildir_unlink(src, msg))
				error = 1;
			break;

		case EXPR_TYPE_LABEL:
			/*
			 * Write message with new labels to the source maildir
			 * and update the message path. This is of importance if
			 * a following action requires a source maildir.
			 */
			if (maildir_write(src, src, msg,
				    tmp, sizeof(tmp), env)) {
				error = 1;
			} else if (maildir_unlink(src, msg)) {
				error = 1;
			} else {
				(void)strlcpy(path, tmp, sizeof(path));
				msg->me_path = path;
			}
			break;

		case EXPR_TYPE_REJECT:
			*reject = 1;
			break;

		case EXPR_TYPE_EXEC: {
			int fd = -1;

			if (mh->mh_expr->ex_exec.e_flags & EXPR_EXEC_STDIN) {
				fd = message_get_fd(msg);
				if (fd == -1) {
					error = 1;
					break;
				}
			}
			error = exec(mh->mh_exec, fd);
			break;
		}

		default:
			break;
		}

		if (error)
			break;
	}

	maildir_close(dst);
	msg->me_path = path_save;

	return error;
}

int
matches_inspect(const struct match_list *ml, const struct message *msg,
    FILE *fh, const struct environment *env)
{
	const struct match *mh;
	const char *path = NULL;

	/* Find the last non-empty path. */
	TAILQ_FOREACH(mh, ml, mh_entry) {
		if (mh->mh_path[0] == '\0')
			continue;

		path = mh->mh_path;
	}
	assert(path != NULL);

	if (env->ev_options & OPTION_STDIN)
		log_info("<stdin> -> %s\n", path);
	else
		log_info("%s -> %s\n", msg->me_path, path);

	if ((env->ev_options & OPTION_DRYRUN) == 0)
		return 0;

	TAILQ_FOREACH(mh, ml, mh_entry) {
		expr_inspect(mh->mh_expr, fh, env);
	}

	return 1;
}

struct match *
matches_find(struct match_list *ml, enum expr_type type)
{
	struct match *mh;

	TAILQ_FOREACH(mh, ml, mh_entry) {
		if (mh->mh_expr->ex_type == type)
			return mh;
	}

	return NULL;
}

/*
 * Remove all expressions with the given type from the match list.
 * Returns the number of actions left in the match list.
 */
int
matches_remove(struct match_list *ml, enum expr_type type)
{
	struct match *mh, *tmp;
	int n = 0;

	TAILQ_FOREACH_SAFE(mh, ml, mh_entry, tmp) {
		const struct expr *ex = mh->mh_expr;

		if (ex->ex_type == type)
			TAILQ_REMOVE(ml, mh, mh_entry);
		else if (ex->ex_flags & EXPR_FLAG_ACTION)
			n++;
	}

	return n;
}

void
match_copy(struct match *mh, const char *str, const regmatch_t *off,
    size_t nmemb)
{
	char *cpy;
	size_t i, len;

	match_reset(mh);

	mh->mh_matches = reallocarray(NULL, nmemb, sizeof(*mh->mh_matches));
	if (mh->mh_matches == NULL)
		err(1, NULL);
	mh->mh_nmatches = nmemb;
	for (i = 0; i < nmemb; i++) {
		size_t j;

		len = off[i].rm_eo - off[i].rm_so;
		cpy = strndup(str + off[i].rm_so, len);
		if (cpy == NULL)
			err(1, NULL);
		if (mh->mh_expr->ex_re.r_flags & EXPR_PATTERN_LCASE) {
			for (j = 0; cpy[j] != '\0'; j++)
				cpy[j] = tolower((unsigned char)cpy[j]);
		}
		if (mh->mh_expr->ex_re.r_flags & EXPR_PATTERN_UCASE) {
			for (j = 0; cpy[j] != '\0'; j++)
				cpy[j] = toupper((unsigned char)cpy[j]);
		}
		mh->mh_matches[i] = cpy;
	}
}

void
match_reset(struct match *mh)
{
	unsigned int i;

	mh->mh_path[0] = mh->mh_maildir[0] = mh->mh_subdir[0] = '\0';

	for (i = 0; i < mh->mh_nmatches; i++)
		free(mh->mh_matches[i]);
	free(mh->mh_matches);
	mh->mh_matches = NULL;
	mh->mh_nmatches = 0;

	free(mh->mh_key);
	mh->mh_key = NULL;
	free(mh->mh_val);
	mh->mh_val = NULL;
}

static const struct match *
matches_find_interpolate(const struct match_list *ml)
{
	const struct match *mh;
	const struct match *found = NULL;

	TAILQ_FOREACH(mh, ml, mh_entry) {
		const struct expr *ex = mh->mh_expr;

		if ((ex->ex_flags & EXPR_FLAG_INTERPOLATE) == 0)
			continue;

		if (ex->ex_re.r_flags & EXPR_PATTERN_FORCE) {
			found = mh;
			break;
		}
		if (found == NULL)
			found = mh;
	}

	return found;
}

static void
matches_merge(struct match_list *ml, struct match *mh)
{
	struct match *dup;

	if (mh->mh_expr->ex_type != EXPR_TYPE_MOVE &&
	    mh->mh_expr->ex_type != EXPR_TYPE_FLAG)
		return;

	/*
	 * Merge consecutive flag actions, the last flag action dictates the
	 * flag state anyway.
	 */
	if (mh->mh_expr->ex_type == EXPR_TYPE_FLAG) {
		dup = TAILQ_LAST(ml, match_list);
		if (dup != NULL && dup->mh_expr->ex_type == EXPR_TYPE_FLAG) {
			TAILQ_REMOVE(ml, dup, mh_entry);
			return;
		}
	}

	/*
	 * A message only needs to moved or flagged once since both actions
	 * refer to the same destination maildir.
	 */
	dup = matches_find(ml, mh->mh_expr->ex_type == EXPR_TYPE_MOVE ?
	    EXPR_TYPE_FLAG : EXPR_TYPE_MOVE);
	if (dup == NULL)
		return;
	TAILQ_REMOVE(ml, dup, mh_entry);

	if (mh->mh_expr->ex_type == EXPR_TYPE_MOVE) {
		/* Copy subdir from flag action. */
		(void)strlcpy(mh->mh_subdir, dup->mh_subdir,
		    sizeof(mh->mh_subdir));
	} else {
		/* Copy maildir from move action. */
		(void)strlcpy(mh->mh_maildir, dup->mh_maildir,
		    sizeof(mh->mh_maildir));
	}
}

static const char *
match_get(const struct match *mh, unsigned int idx)
{

	if (mh == NULL || idx >= mh->mh_nmatches)
		return NULL;
	return mh->mh_matches[idx];
}

static int
isbackref(const char *str, unsigned int *br)
{
	char *end;
	unsigned long val;

	if (str[0] != '\\' || !isdigit((unsigned char)str[1]))
		return 0;

	val = strtoul(&str[1], &end, 10);
	if (val > INT_MAX)
		return -1;

	*br = val;
	return end - str;
}

static int
exec(char *const *argv, int fdin)
{
	pid_t pid;
	int error = 1;
	int status;
	int doclose = 0;

	if (fdin == -1) {
		doclose = 1;
		fdin = open("/dev/null", O_RDONLY | O_CLOEXEC);
		if (fdin == -1) {
			warn("open: /dev/null");
			goto out;
		}
	}

	pid = fork();
	if (pid == -1) {
		warn("fork");
		goto out;
	}
	if (pid == 0) {
		if (dup2(fdin, 0) == -1)
			err(1, "dup2");
		execvp(argv[0], argv);
		warn("%s", argv[0]);
		_exit(1);
	}

	if (waitpid(pid, &status, 0) == -1) {
		warn("waitpid");
		goto out;
	}
	if (WIFEXITED(status))
		error = WEXITSTATUS(status);
	if (WIFSIGNALED(status))
		error = WTERMSIG(status);
	log_debug("%s: command = \"%s\", exit=%d, signal=%d\n",
	    __func__, argv[0],
	    WIFEXITED(status) ? WEXITSTATUS(status) : -1,
	    WIFSIGNALED(status) ? WTERMSIG(status) : -1);

out:
	if (doclose && fdin != -1)
		close(fdin);
	return error;
}

/*
 * Interpolate the given string, storing it in buf.
 */
static int
interpolate(const struct match *mh, const char *str, char **buf)
{
	size_t buflen = 0;
	size_t bufsiz = 0;
	size_t i = 0;

	while (str[i] != '\0') {
		const char *sub;
		unsigned int br;
		int n;

		n = isbackref(&str[i], &br);
		if (n < 0)
			goto invalid;
		if (n > 0) {
			sub = match_get(mh, br);
			if (sub == NULL)
				goto invalid;

			append(buf, &bufsiz, &buflen, sub);
			i += n;
		} else {
			appendc(buf, &bufsiz, &buflen, str[i]);
			i++;
		}
	}

	return 0;

invalid:
	warnx("%s: invalid back-reference", str);
	return 1;
}
