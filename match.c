#include "config.h"

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

static const struct match *matches_find_interpolate(const struct match_list *);

static const char *match_get(const struct match *, unsigned long);

void
matches_append(struct match_list *ml, struct match *mh)
{
	enum expr_type type = mh->mh_expr->type;

	/*
	 * A message only needs to moved or flagged once since both actions
	 * refer to the same destination.
	 */
	if ((type == EXPR_TYPE_MOVE || type == EXPR_TYPE_FLAG) &&
	    (matches_find(ml, EXPR_TYPE_MOVE) || matches_find(ml, EXPR_TYPE_FLAG)))
		return;

	if (type == EXPR_TYPE_LABEL)
		TAILQ_INSERT_HEAD(&ml->ml_head, mh, mh_entry);
	else
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
matches_interpolate(struct match_list *ml, const struct message *msg)
{
	char buf[PATH_MAX];
	const struct match *mh;
	const char *sub;
	char *end, *path;
	unsigned long bf;
	size_t i = 0;
	size_t j = 0;

	/* Discard is mutually exclusive with all other actions. */
	if (matches_find(ml, EXPR_TYPE_DISCARD) != NULL)
		return 0;

	if (ml->ml_maildir[0] == '\0') {
		/* No maildir present, infer from message path. */
		if (pathslice(msg->path, ml->ml_maildir, 0, -2) == NULL)
			errx(1, "%s: %s: maildir not found",
			    __func__, msg->path);
	}

	if (ml->ml_subdir[0] == '\0') {
		/* No subdir present, infer from message path. */
		if (pathslice(msg->path, ml->ml_subdir, -2, -2) == NULL)
			errx(1, "%s: %s: subdir not found",
			    __func__, msg->path);
	}

	path = pathjoin(buf, ml->ml_maildir, ml->ml_subdir, NULL);

	mh = matches_find_interpolate(ml);
	while (path[i] != '\0') {
		if (path[i] == '\\' && isdigit(path[i + 1])) {
			errno = 0;
			bf = strtoul(path + i + 1, &end, 10);
			if ((errno == ERANGE && bf == ULONG_MAX) ||
			    ((sub = match_get(mh, bf)) == NULL)) {
				warnx("%s: invalid back-reference in "
				    "destination", path);
				return 1;
			}
			for (; *sub != '\0'; sub++) {
				if (j == sizeof(ml->ml_path) - 1)
					goto toolong;
				ml->ml_path[j++] = *sub;
			}
			i = end - path;
			continue;
		}
		if (j == sizeof(ml->ml_path) - 1)
			goto toolong;
		ml->ml_path[j++] = path[i++];
	}
	assert(j < sizeof(ml->ml_path));
	ml->ml_path[j] = '\0';
	return 0;

toolong:
	warnx("%s: destination too long", path);
	return 1;
}

int
matches_exec(const struct match_list *ml, struct maildir *src,
    struct message *msg, const struct environment *env)
{
	char path[NAME_MAX];
	struct maildir *dst;
	struct match *mh;
	const char *path_save;
	int error = 0;

	path_save = msg->path;

	TAILQ_FOREACH(mh, &ml->ml_head, mh_entry) {
		switch (mh->mh_expr->type) {
		case EXPR_TYPE_FLAG:
		case EXPR_TYPE_MOVE:
			dst = maildir_open(ml->ml_path, 0, env);
			if (dst == NULL) {
				error = 1;
				break;
			}
			if (maildir_move(src, dst, msg, env))
				error = 1;
			maildir_close(dst);
			break;
		case EXPR_TYPE_DISCARD:
			if (maildir_unlink(src, msg))
				error = 1;
			break;
		case EXPR_TYPE_LABEL:
			/*
			 * Write message with new labels to the source maildir
			 * and update the message path. This is of importance
			 * if a move or flag action is up next.
			 */
			if (maildir_write(src, src, msg,
			    path, sizeof(path), env))
				error = 1;
			else if (maildir_unlink(src, msg))
				error = 1;
			else
				msg->path = path;
			break;
		default:
			break;
		}

		if (error)
			break;
	}

	msg->path = path_save;

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
		if (mh->mh_expr->type == type)
			return mh;
	}

	return NULL;
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

	TAILQ_FOREACH(mh, &ml->ml_head, mh_entry) {
		if (mh->mh_nmatches > 0)
			return mh;
	}

	return NULL;
}

static const char *
match_get(const struct match *mh, unsigned long n)
{
	if (mh == NULL || n >= mh->mh_nmatches)
		return NULL;
	return mh->mh_matches[n];
}
