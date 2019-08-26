#include "config.h"

#include <err.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

static const struct match *matches_find_interpolate(const struct match_list *);

static unsigned char match_char(const struct match *, unsigned char);
static const char *match_get(const struct match *, unsigned int);

static int backref(const char *, unsigned int *);
static int bufgrow(char **, size_t *, size_t, int);
static char *interpolate(const struct match *, const char *, char *, size_t,
    int);

void
matches_append(struct match_list *ml, struct match *mh)
{
	enum expr_type type = mh->mh_expr->ex_type;

	/*
	 * A message only needs to moved or flagged once since both actions
	 * refer to the same destination.
	 */
	if ((type == EXPR_TYPE_MOVE || type == EXPR_TYPE_FLAG) &&
	    (matches_find(ml, EXPR_TYPE_MOVE) || matches_find(ml, EXPR_TYPE_FLAG)))
		return;

	TAILQ_INSERT_TAIL(&ml->ml_head, mh, mh_entry);
}

void
matches_clear(struct match_list *ml)
{
	struct match *mh;

	while ((mh = TAILQ_FIRST(&ml->ml_head)) != NULL) {
		TAILQ_REMOVE(&ml->ml_head, mh, mh_entry);
	}

	ml->ml_maildir[0] = ml->ml_subdir[0] = ml->ml_path[0] = '\0';
}

int
matches_interpolate(struct match_list *ml, struct message *msg)
{
	char buf[PATH_MAX];
	const struct match *mh;
	const char *str;
	char *label, *path;
	size_t len;

	/*
	 * Some actions sets the path during expression evaluation and are
	 * therefore not eligible for interpolation.
	 */
	if (ml->ml_path[0] != '\0')
		return 0;

	if (ml->ml_maildir[0] == '\0') {
		/* No maildir present, infer from message path. */
		len = sizeof(ml->ml_maildir);
		if (pathslice(msg->me_path, ml->ml_maildir, len, 0, -2) == NULL) {
			warnx("%s: %s: maildir not found",
			    __func__, msg->me_path);
			return 1;
		}
	}
	if (ml->ml_subdir[0] == '\0') {
		/* No subdir present, infer from message path. */
		len = sizeof(ml->ml_subdir);
		if (pathslice(msg->me_path, ml->ml_subdir, len, -2, -2) == NULL) {
			warnx("%s: %s: subdir not found",
			    __func__, msg->me_path);
			return 1;
		}
	}
	path = pathjoin(buf, sizeof(buf), ml->ml_maildir, ml->ml_subdir);
	if (path == NULL) {
		warnc(ENAMETOOLONG, "%s", __func__);
		return 1;
	}

	mh = matches_find_interpolate(ml);
	len = sizeof(ml->ml_path);
	if (interpolate(mh, path, ml->ml_path, len, 0) == NULL)
		return 1;

	if (matches_find(ml, EXPR_TYPE_LABEL)) {
		str = message_get_header1(msg, "X-Label");
		if (str == NULL) {
			/*
			 * This should never happen since a label action always
			 * sets the X-Label header in expr_eval_label(). But
			 * some static analysis tools interpret usage of str
			 * below as a potential NULL deference.
			 */
			return 1;
		}
		len = strlen(str) + 1;
		label = malloc(len);
		if (label == NULL)
			err(1, NULL);
		label = interpolate(mh, str, label, len, 1);
		if (label == NULL)
			return 1;
		message_set_header(msg, "X-Label", DISOWN(label));
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

	TAILQ_FOREACH(mh, &ml->ml_head, mh_entry) {
		switch (mh->mh_expr->ex_type) {
		case EXPR_TYPE_FLAG:
		case EXPR_TYPE_MOVE:
			/*
			 * Move message and update the source maildir and
			 * message path. This is of importance if a following
			 * action requires a source maildir.
			 */
			maildir_close(dst);
			dst = maildir_open(ml->ml_path, 0, env);
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

void
matches_inspect(const struct match_list *ml, FILE *fh,
    const struct environment *env)
{
	const struct match *mh;

	TAILQ_FOREACH(mh, &ml->ml_head, mh_entry) {
		expr_inspect(mh->mh_expr, fh, env);
	}
}

struct match *
matches_find(struct match_list *ml, enum expr_type type)
{
	struct match *mh;

	TAILQ_FOREACH(mh, &ml->ml_head, mh_entry) {
		if (mh->mh_expr->ex_type == type)
			return mh;
	}

	return NULL;
}

void
matches_remove(struct match_list *ml, struct match *mh)
{
	TAILQ_REMOVE(&ml->ml_head, mh, mh_entry);
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

	TAILQ_FOREACH(mh, &ml->ml_head, mh_entry) {
		const struct expr *ex = mh->mh_expr;

		if ((ex->ex_flags & EXPR_FLAG_INTERPOLATE) == 0)
			continue;

		if (ex->ex_re.r_flags & EXPR_PATTERN_FORCE)
			found = mh;
		else if (found == NULL)
			found = mh;
	}

	return found;
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

	*buf = reallocarray(*buf, 2, *bufsiz);
	if (*buf == NULL)
		err(1, NULL);
	*bufsiz *= 2;

	return 0;
}

static char *
interpolate(const struct match *mh, const char *str, char *buf, size_t bufsiz,
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
				if (bufgrow(&buf, &bufsiz, buflen, grow))
					goto toolong;

				buf[buflen++] = match_char(mh, *sub);
			}
			i += n;
			continue;
		}

		if (bufgrow(&buf, &bufsiz, buflen, grow))
			goto toolong;
		buf[buflen++] = str[i++];
	}
	buf[buflen] = '\0';

	return buf;

invalid:
	warnx("%s: invalid back-reference", str);
	if (grow)
		free(buf);
	return NULL;

toolong:
	warnx("%s: interpolated string too long", str);
	if (grow)
		free(buf);
	return NULL;
}
