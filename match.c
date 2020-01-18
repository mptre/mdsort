#include "config.h"

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

static const struct match *matches_find_interpolate(const struct match_list *);
static void matches_merge(struct match_list *, struct match *);

static unsigned char match_char(const struct match *, unsigned char);
static const char *match_get(const struct match *, unsigned int);

static int backref(const char *, unsigned int *);
static int bufgrow(char **, size_t *, size_t, int);
static int interpolate(const struct match *, const char *, char **, size_t,
    int);

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
		mh->mh_path[0] = mh->mh_maildir[0] = mh->mh_subdir[0] = '\0';
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
			char buf[PATH_MAX];
			char *tmp = buf;

			if (interpolate(mi, mh->mh_path, &tmp, sizeof(buf), 0))
				return 1;
			(void)strlcpy(mh->mh_path, tmp, sizeof(mh->mh_path));
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
			if (interpolate(mi, str, &label, 0, 1)) {
				free(label);
				return 1;
			}
			message_set_header(msg, "X-Label", label);
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
			} else {
				(void)strlcpy(path, tmp, sizeof(path));
				msg->me_path = path;
			}
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

void
matches_remove(struct match_list *ml, struct match *mh)
{

	TAILQ_REMOVE(ml, mh, mh_entry);
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
		len = off[i].rm_eo - off[i].rm_so;
		cpy = strndup(str + off[i].rm_so, len);
		if (cpy == NULL)
			err(1, NULL);
		mh->mh_matches[i] = cpy;
	}
}

void
match_reset(struct match *mh)
{
	unsigned int i;

	if (mh == NULL)
		return;

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

	/*
	 * A message only needs to moved or flagged once since both actions
	 * refer to the same destination maildir.
	 */
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
			matches_remove(ml, dup);
			return;
		}
	}

	dup = matches_find(ml, mh->mh_expr->ex_type == EXPR_TYPE_MOVE ?
	    EXPR_TYPE_FLAG : EXPR_TYPE_MOVE);
	if (dup == NULL)
		return;
	matches_remove(ml, dup);

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

static unsigned char
match_char(const struct match *mh, unsigned char c)
{

	if (mh->mh_expr->ex_re.r_flags & EXPR_PATTERN_LCASE)
		return tolower(c);
	if (mh->mh_expr->ex_re.r_flags & EXPR_PATTERN_UCASE)
		return toupper(c);
	return c;
}

static const char *
match_get(const struct match *mh, unsigned int n)
{

	if (mh == NULL || n >= mh->mh_nmatches)
		return NULL;
	return mh->mh_matches[n];
}

static int
backref(const char *str, unsigned int *br)
{
	char *end;
	unsigned long val;

	if (str[0] != '\\' || !isdigit(str[1]))
		return 0;

	val = strtoul(&str[1], &end, 10);
	if (val > INT_MAX)
		return -1;

	*br = val;
	return end - str;
}

static int
bufgrow(char **buf, size_t *bufsiz, size_t buflen, int grow)
{

	if (*bufsiz > 0 && buflen < *bufsiz - 1)
		return 0;
	if (!grow)
		return 1;

	if (*bufsiz == 0)
		*bufsiz = 128;
	*buf = reallocarray(*buf, 2, *bufsiz);
	if (*buf == NULL)
		err(1, NULL);
	*bufsiz *= 2;

	return 0;
}

static int
interpolate(const struct match *mh, const char *str, char **buf, size_t bufsiz,
    int grow)
{
	const char *sub;
	size_t buflen = 0;
	size_t i = 0;
	unsigned int br;
	int n;

	while (str[i] != '\0') {
		n = backref(&str[i], &br);
		if (n < 0)
			goto invalid;
		if (n > 0) {
			sub = match_get(mh, br);
			if (sub == NULL)
				goto invalid;

			for (; *sub != '\0'; sub++) {
				if (bufgrow(buf, &bufsiz, buflen, grow))
					goto toolong;

				(*buf)[buflen++] = match_char(mh, *sub);
			}
			i += n;
			continue;
		}

		if (bufgrow(buf, &bufsiz, buflen, grow))
			goto toolong;
		(*buf)[buflen++] = str[i++];
	}
	(*buf)[buflen] = '\0';

	return 0;

invalid:
	warnx("%s: invalid back-reference", str);
	return 1;

toolong:
	warnx("%s: interpolated string too long", str);
	return 1;
}
