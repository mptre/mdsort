#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "buffer.h"
#include "extern.h"
#include "macro.h"
#include "message.h"
#include "vector.h"

struct backref {
	unsigned int br_mi;	/* match index */
	unsigned int br_si;	/* subexpression index */
};

static void	matches_merge(struct match_list *, struct match *);

static const char	*match_backref(const struct match *,
    const struct backref *);

static char	*interpolate(const struct match *, const struct macro_list *,
    const char *);
static ssize_t	 isbackref(const char *, struct backref *);

/*
 * Append the given match to the list and construct the maildir destination path
 * if needed.
 */
int
matches_append(struct match_list *ml, struct match *mh)
{
	const char *p, *path;
	size_t siz;

	matches_merge(ml, mh);
	TAILQ_INSERT_TAIL(ml, mh, mh_entry);

	if ((mh->mh_expr->ex_flags & EXPR_FLAG_PATH) == 0)
		return 0;

	path = message_get_path(mh->mh_msg);
	if (mh->mh_maildir[0] == '\0') {
		/* Infer maildir from message path. */
		siz = sizeof(mh->mh_maildir);
		p = pathslice(path, mh->mh_maildir, siz, 0, -2);
		if (p == NULL) {
			warnx("%s: %s: maildir not found", __func__, path);
			return 1;
		}
	}
	if (mh->mh_subdir[0] == '\0') {
		/* Infer subdir from message path. */
		siz = sizeof(mh->mh_subdir);
		p = pathslice(path, mh->mh_subdir, siz, -2, -2);
		if (p == NULL) {
			warnx("%s: %s: subdir not found", __func__, path);
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
	struct macro_list *macros;
	struct match *mh;
	int error = 0;

	macros = macros_alloc(MACRO_CTX_ACTION);
	/* Construct action macro context. */
	macros_insertc(macros, "path",
	    message_get_path(TAILQ_FIRST(ml)->mh_msg));

	TAILQ_FOREACH(mh, ml, mh_entry) {
		if (match_interpolate(mh, macros)) {
			error = 1;
			break;
		}
	}

	macros_free(macros);
	return error;
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
		case EXPR_TYPE_FLAGS:
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
			if (maildir_unlink(src, message_get_name(msg)))
				error = 1;
			break;

		case EXPR_TYPE_LABEL:
		case EXPR_TYPE_ADD_HEADER:
			if (maildir_write(src, msg, env))
				error = 1;
			break;

		case EXPR_TYPE_REJECT:
			*reject = 1;
			break;

		case EXPR_TYPE_EXEC: {
			unsigned int flags = mh->mh_expr->ex_exec.flags;
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
			if (error > 0) {
				warnx("%s: exited %d", mh->mh_exec[0], error);
			} else if (error < 0) {
				/* Warning already emitted by exec(). */
				error = 1;
			}
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
		const char *path;

		if ((ex->ex_flags & EXPR_FLAG_ACTION) == 0)
			continue;

		path = message_get_path(msg);
		log_info("%s -> %s\n",
		    env->ev_options & OPTION_STDIN ? "<stdin>" : path,
		    ex->ex_label ? ex->ex_label : mh->mh_path);

		if (!dryrun)
			continue;

		/* Handle all matchers leading up to this action. */
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
	size_t i;

	mh->mh_matches = reallocarray(NULL, nmemb, sizeof(*mh->mh_matches));
	if (mh->mh_matches == NULL)
		err(1, NULL);
	mh->mh_nmatches = nmemb;
	for (i = 0; i < nmemb; i++) {
		size_t eo, j, len, so;

		so = (size_t)off[i].rm_so;
		eo = (size_t)off[i].rm_eo;
		len = eo - so;
		cpy = strndup(str + off[i].rm_so, len);
		if (cpy == NULL)
			err(1, NULL);
		if (mh->mh_expr->ex_re.flags & EXPR_PATTERN_LCASE) {
			for (j = 0; cpy[j] != '\0'; j++)
				cpy[j] = tolower((unsigned char)cpy[j]);
		}
		if (mh->mh_expr->ex_re.flags & EXPR_PATTERN_UCASE) {
			for (j = 0; cpy[j] != '\0'; j++)
				cpy[j] = toupper((unsigned char)cpy[j]);
		}
		mh->mh_matches[i].m_str = cpy;
		mh->mh_matches[i].m_beg = so;
		mh->mh_matches[i].m_end = eo;
	}
}

int
match_interpolate(struct match *mh, const struct macro_list *macros)
{
	struct message *msg = mh->mh_msg;

	switch (mh->mh_expr->ex_type) {
	case EXPR_TYPE_STAT:
	case EXPR_TYPE_MOVE: {
		char *path;
		size_t n, siz;

		path = interpolate(mh, macros, mh->mh_path);
		if (path == NULL)
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
		VECTOR(char *const) labels;
		struct buffer *bf;
		const struct string *str;
		char *label = NULL;
		char *buf;

		bf = buffer_alloc(128);
		if (bf == NULL)
			err(1, NULL);

		labels = message_get_header(msg, "X-Label");
		if (labels != NULL) {
			size_t i;

			for (i = 0; i < VECTOR_LENGTH(labels); i++) {
				if (i > 0)
					buffer_putc(bf, ' ');
				buffer_printf(bf, "%s", labels[i]);
			}
		}
		TAILQ_FOREACH(str, mh->mh_expr->ex_strings, entry) {
			if (buffer_get_len(bf) > 0)
				buffer_putc(bf, ' ');
			buffer_printf(bf, "%s", str->val);
		}
		buffer_putc(bf, '\0');
		buf = buffer_release(bf);
		buffer_free(bf);
		label = interpolate(mh, macros, buf);
		free(buf);
		if (label == NULL)
			return 1;
		message_set_header(msg, "X-Label", label);
		break;
	}

	case EXPR_TYPE_COMMAND:
	case EXPR_TYPE_EXEC: {
		const struct string *str;
		size_t nargs = 0;
		size_t len;

		/* Make room for NULL-terminator. */
		len = strings_len(mh->mh_expr->ex_strings) + 1;
		mh->mh_exec = reallocarray(NULL, len, sizeof(*mh->mh_exec));
		if (mh->mh_exec == NULL)
			err(1, NULL);
		memset(mh->mh_exec, 0, len * sizeof(*mh->mh_exec));
		mh->mh_nexec = len;
		TAILQ_FOREACH(str, mh->mh_expr->ex_strings, entry) {
			char *arg;

			arg = interpolate(mh, macros, str->val);
			if (arg == NULL)
				return 1;
			mh->mh_exec[nargs++] = arg;
		}
		break;
	}

	case EXPR_TYPE_ADD_HEADER: {
		const struct expr *ex = mh->mh_expr;
		char *val;

		val = interpolate(mh, macros, ex->ex_add_header.val);
		if (val == NULL)
			return 1;
		message_set_header(msg, ex->ex_add_header.key, val);
		break;
	}

	case EXPR_TYPE_BLOCK:
	case EXPR_TYPE_AND:
	case EXPR_TYPE_OR:
	case EXPR_TYPE_NEG:
	case EXPR_TYPE_MATCH:
	case EXPR_TYPE_ALL:
	case EXPR_TYPE_ATTACHMENT:
	case EXPR_TYPE_BODY:
	case EXPR_TYPE_DATE:
	case EXPR_TYPE_HEADER:
	case EXPR_TYPE_NEW:
	case EXPR_TYPE_OLD:
	case EXPR_TYPE_FLAG:
	case EXPR_TYPE_FLAGS:
	case EXPR_TYPE_DISCARD:
	case EXPR_TYPE_BREAK:
	case EXPR_TYPE_PASS:
	case EXPR_TYPE_REJECT:
	case EXPR_TYPE_ATTACHMENT_BLOCK:
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
match_backref(const struct match *mh, const struct backref *br)
{
	const struct match *tmp = mh;
	const struct match *mi = NULL;
	unsigned int i = 0;

	/* Go backwards to the start of the given match. */
	for (;;) {
		tmp = TAILQ_PREV(tmp, match_list, mh_entry);
		if (tmp == NULL)
			return NULL;
		if (tmp->mh_expr->ex_type == EXPR_TYPE_MATCH)
			break;
	}

	for (;;) {
		tmp = TAILQ_NEXT(tmp, mh_entry);
		if (tmp == NULL || tmp == mh)
			return NULL;
		if ((tmp->mh_expr->ex_flags & EXPR_FLAG_INTERPOLATE) &&
		    i++ == br->br_mi) {
			mi = tmp;
			break;
		}
	}
	if (mi == NULL || br->br_si >= mi->mh_nmatches)
		return NULL;

	return mi->mh_matches[br->br_si].m_str;
}

static ssize_t
isbackref(const char *str, struct backref *br)
{
	const char *s = str;
	char *end;
	unsigned long val;

	if (s[0] != '\\' || !isdigit((unsigned char)s[1]))
		return 0;

	val = strtoul(&s[1], &end, 10);
	if (val > INT_MAX)
		return -1;
	s = end;

	if (s[0] == '.') {
		br->br_mi = val;
		val = strtoul(&s[1], &end, 10);
		if (val > INT_MAX)
			return -1;
		br->br_si = val;
	} else {
		if (s[0] == '\\' && s[1] == '.')
			end++;
		br->br_mi = 0;
		br->br_si = val;
	}

	return end - str;
}

static char *
interpolate(const struct match *mh, const struct macro_list *macros,
    const char *str)
{
	struct buffer *bf;
	char *buf;
	size_t i = 0;

	bf = buffer_alloc(64);
	if (bf == NULL)
		err(1, NULL);

	while (str[i] != '\0') {
		struct backref br;
		const char *sub;
		char *macro;
		ssize_t n;

		n = isbackref(&str[i], &br);
		if (n < 0)
			goto brerr;
		if (n > 0) {
			sub = match_backref(mh, &br);
			if (sub == NULL)
				goto brerr;

			buffer_printf(bf, "%s", sub);
			i += (size_t)n;
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

			buffer_printf(bf, "%s", macro_get_value(mc));
			i += (size_t)n;
			continue;
		}

		buffer_putc(bf, str[i++]);
	}

	buffer_putc(bf, '\0');
	buf = buffer_release(bf);
	buffer_free(bf);
	return buf;

brerr:
	warnx("%s: invalid back-reference", str);
	buffer_free(bf);
	return NULL;
mcerr:
	warnx("%s: invalid macro", str);
	buffer_free(bf);
	return NULL;
}
