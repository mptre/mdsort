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

static void matches_merge(struct match_list *, struct match *);

static const char *match_get(const struct match *, unsigned int);

static int exec(char *const *, int);
static int interpolate(const struct match *, const struct macro_list *,
    const char *, char **);
static ssize_t isbackref(const char *, unsigned int *);

/*
 * Append the given match to the list and construct the maildir destination path
 * if needed.
 */
int
matches_append(struct match_list *ml, struct match *mh)
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
		p = pathslice(mh->mh_msg->me_path, mh->mh_maildir, siz, 0, -2);
		if (p == NULL) {
			warnx("%s: %s: maildir not found",
			    __func__, mh->mh_msg->me_path);
			return 1;
		}
	}
	if (mh->mh_subdir[0] == '\0') {
		/* Infer subdir from message path. */
		siz = sizeof(mh->mh_subdir);
		p = pathslice(mh->mh_msg->me_path, mh->mh_subdir, siz, -2, -2);
		if (p == NULL) {
			warnx("%s: %s: subdir not found",
			    __func__, mh->mh_msg->me_path);
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
		TAILQ_REMOVE(ml, mh, mh_entry);
		match_free(mh);
	}

}

int
matches_interpolate(struct match_list *ml)
{
	struct macro_list macros;
	struct match *mh;

	/* Construct action macro context. */
	macros_init(&macros, MACRO_CTX_ACTION);
	macros_insertc(&macros, "path", TAILQ_FIRST(ml)->mh_msg->me_path);

	TAILQ_FOREACH(mh, ml, mh_entry) {
		if (match_interpolate(mh, &macros))
			return 1;
	}

	return 0;
}

int
matches_exec(const struct match_list *ml, struct maildir *src, int *reject,
    const struct environment *env)
{
	struct maildir *dst = NULL;
	struct match *mh;
	int chsrc = 0;
	int error = 0;

	TAILQ_FOREACH(mh, ml, mh_entry) {
		struct message *msg = mh->mh_msg;

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
			dst = maildir_open(mh->mh_path, 0, env);
			if (dst == NULL) {
				error = 1;
				break;
			}

			if (maildir_move(src, dst, msg, env)) {
				maildir_close(dst);
				error = 1;
				break;
			}

			if (maildir_cmp(src, dst)) {
				if (chsrc)
					maildir_close(src);
				chsrc = 1;
				src = dst;
			} else {
				maildir_close(dst);
			}
			dst = NULL;
			break;

		case EXPR_TYPE_DISCARD:
			if (maildir_unlink(src, msg->me_name))
				error = 1;
			break;

		case EXPR_TYPE_LABEL:
			if (maildir_write(src, msg, env))
				error = 1;
			break;

		case EXPR_TYPE_REJECT:
			*reject = 1;
			break;

		case EXPR_TYPE_EXEC: {
			unsigned int flags = mh->mh_expr->ex_exec.e_flags;
			int fd = -1;

			if (flags & EXPR_EXEC_STDIN) {
				fd = message_get_fd(msg, env,
				    flags & EXPR_EXEC_BODY);
				if (fd == -1) {
					error = 1;
					break;
				}
			}
			error = exec(mh->mh_exec, fd);
			if (fd != -1)
				close(fd);
			break;
		}

		default:
			break;
		}

		if (error)
			break;
	}

	if (chsrc)
		maildir_close(src);

	return error;
}

int
matches_inspect(const struct match_list *ml, const struct environment *env)
{
	const struct match *lhs, *mh;
	const struct message *msg;
	int dryrun = env->ev_options & OPTION_DRYRUN;

	lhs = TAILQ_FIRST(ml);
	msg = TAILQ_FIRST(ml)->mh_msg;

	TAILQ_FOREACH(mh, ml, mh_entry) {
		const struct expr *ex = mh->mh_expr;
		const struct match *rhs;

		if ((ex->ex_flags & EXPR_FLAG_ACTION) == 0)
			continue;

		log_info("%s -> %s\n",
		    env->ev_options & OPTION_STDIN ? "<stdin>" : msg->me_path,
		    ex->ex_label ? ex->ex_label : mh->mh_path);

		if (!dryrun)
			continue;

		rhs = lhs;
		for (;;) {
			if (rhs == mh)
				break;
			expr_inspect(rhs->mh_expr, rhs, env);
			rhs = TAILQ_NEXT(rhs, mh_entry);
		}
		lhs = rhs;
	}

	return dryrun;
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

		if (ex->ex_type == type) {
			TAILQ_REMOVE(ml, mh, mh_entry);
			match_free(mh);
		} else if (ex->ex_flags & EXPR_FLAG_ACTION) {
			n++;
		}
	}

	return n;
}

struct match *
match_alloc(struct expr *ex, struct message *msg)
{
	struct match *mh;

	mh = calloc(1, sizeof(*mh));
	if (mh == NULL)
		err(1, NULL);
	mh->mh_expr = ex;
	mh->mh_msg = msg;
	return mh;
}

void
match_free(struct match *mh)
{
	unsigned int i;

	if (mh == NULL)
		return;

	for (i = 0; i < mh->mh_nmatches; i++)
		free(mh->mh_matches[i].m_str);
	free(mh->mh_matches);

	for (i = 0; i < mh->mh_nexec; i++)
		free(mh->mh_exec[i]);
	free(mh->mh_exec);

	free(mh->mh_key);
	free(mh->mh_val);
	free(mh);
}

void
match_copy(struct match *mh, const char *str, const regmatch_t *off,
    size_t nmemb)
{
	char *cpy;
	size_t i, len;

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
		mh->mh_matches[i].m_str = cpy;
		mh->mh_matches[i].m_beg = off[i].rm_so;
		mh->mh_matches[i].m_end = off[i].rm_eo;
	}
}

int
match_interpolate(struct match *mh, const struct macro_list *macros)
{
	const struct match *mi = NULL;
	const struct match *tmp = mh;
	struct message *msg = mh->mh_msg;

	/*
	 * Search backwards for the last expr that can be used for
	 * interpolation.
	 */
	for (;;) {
		const struct expr *ex = tmp->mh_expr;

		if (ex->ex_type == EXPR_TYPE_MATCH)
			break;
		if (ex->ex_flags & EXPR_FLAG_INTERPOLATE)
			mi = tmp;

		tmp = TAILQ_PREV(tmp, match_list, mh_entry);
		if (tmp == NULL)
			break;
	}

	switch (mh->mh_expr->ex_type) {
	case EXPR_TYPE_STAT:
	case EXPR_TYPE_MOVE: {
		char *path = NULL;
		size_t n, siz;

		if (interpolate(mi, macros, mh->mh_path, &path))
			return 1;
		siz = sizeof(mh->mh_path);
		n = strlcpy(mh->mh_path, path, siz);
		free(path);
		if (n >= siz) {
			warnc(ENAMETOOLONG, "%s", __func__);
			return 1;
		}
		break;
	}

	case EXPR_TYPE_LABEL: {
		const struct string_list *labels;
		char *buf = NULL;
		char *label = NULL;
		size_t buflen = 0;
		size_t bufsiz = 0;
		int error;

		labels = message_get_header(msg, "X-Label");
		if (labels != NULL)
			buf = strings_concat(labels, buf, &bufsiz, &buflen);
		buf = strings_concat(mh->mh_expr->ex_strings, buf, &bufsiz,
		    &buflen);
		error = interpolate(mi, macros, buf, &label);
		free(buf);
		if (error)
			return 1;
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

			if (interpolate(mi, macros, str->val, &arg))
				return 1;
			mh->mh_exec[nargs++] = arg;
		}
		break;

	}

	default:
		break;
	}

	return 0;
}

static void
matches_merge(struct match_list *ml, struct match *mh)
{
	const struct expr *ex = mh->mh_expr;
	struct match *dup;

	if (ex->ex_type != EXPR_TYPE_MOVE && ex->ex_type != EXPR_TYPE_FLAG)
		return;

	/*
	 * Merge consecutive flag and move actions, the last action dictates the
	 * destination maildir anyway.
	 */
	dup = TAILQ_LAST(ml, match_list);
	if (dup != NULL && dup->mh_expr->ex_type == ex->ex_type) {
		TAILQ_REMOVE(ml, dup, mh_entry);
		match_free(dup);
		return;
	}

	/*
	 * A message only needs to moved or flagged once since both actions
	 * refer to the same destination maildir.
	 */
	dup = matches_find(ml, ex->ex_type == EXPR_TYPE_MOVE ?
	    EXPR_TYPE_FLAG : EXPR_TYPE_MOVE);
	if (dup == NULL)
		return;

	if (ex->ex_type == EXPR_TYPE_MOVE) {
		/* Copy subdir from flag action. */
		(void)strlcpy(mh->mh_subdir, dup->mh_subdir,
		    sizeof(mh->mh_subdir));
	} else {
		/* Copy maildir from move action. */
		(void)strlcpy(mh->mh_maildir, dup->mh_maildir,
		    sizeof(mh->mh_maildir));
	}

	TAILQ_REMOVE(ml, dup, mh_entry);
	match_free(dup);
}

static const char *
match_get(const struct match *mh, unsigned int idx)
{

	if (idx >= mh->mh_nmatches)
		return NULL;
	return mh->mh_matches[idx].m_str;
}

static ssize_t
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
	if (WIFEXITED(status)) {
		error = WEXITSTATUS(status);
		if (error)
			warnx("%s: %s: exited %d", __func__, argv[0], error);
	}
	if (WIFSIGNALED(status)) {
		error = WTERMSIG(status);
		if (error)
			warnx("%s: %s: killed by signal %d",
			    __func__, argv[0], error);
	}

out:
	if (doclose && fdin != -1)
		close(fdin);
	return error;
}

/*
 * Interpolate the given string, storing it in buf.
 */
static int
interpolate(const struct match *mh, const struct macro_list *macros,
    const char *str, char **buf)
{
	size_t buflen = 0;
	size_t bufsiz = 0;
	size_t i = 0;

	while (str[i] != '\0') {
		const char *sub;
		char *macro;
		ssize_t n;
		unsigned int br;

		n = isbackref(&str[i], &br);
		if (n < 0)
			goto brerr;
		if (n > 0) {
			if (mh == NULL)
				goto brerr;
			sub = match_get(mh, br);
			if (sub == NULL)
				goto brerr;

			append(buf, &bufsiz, &buflen, sub);
			i += n;
			continue;
		}

		n = ismacro(&str[i], &macro);
		if (n < 0)
			goto mcerr;
		if (n > 0) {
			const struct macro *mc;

			mc = macros_find(macros, macro);
			free(macro);
			if (mc == NULL)
				goto mcerr;

			append(buf, &bufsiz, &buflen, mc->mc_value);
			i += n;
			continue;
		}

		appendc(buf, &bufsiz, &buflen, str[i]);
		i++;
	}

	return 0;

brerr:
	warnx("%s: invalid back-reference", str);
	free(*buf);
	*buf = NULL;
	return 1;
mcerr:
	warnx("%s: invalid macro", str);
	free(*buf);
	*buf = NULL;
	return 1;
}
