#include "match.h"

#include "config.h"

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>	/* INT_MAX */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libks/arena-buffer.h"
#include "libks/arena.h"
#include "libks/buffer.h"
#include "libks/list.h"
#include "libks/vector.h"

#include "environment.h"
#include "expr.h"
#include "log.h"
#include "macro.h"
#include "maildir.h"
#include "message.h"
#include "string-list.h"
#include "util.h"

struct backref {
	unsigned int br_mi;	/* match index */
	unsigned int br_si;	/* subexpression index */
};

static void	matches_merge(struct match_list *, struct match *);

static const char	*match_backref(const struct match *,
    const struct backref *);

static const char	*interpolate(const struct match *,
    const struct macro_list *, struct arena *, const char *,
    struct arena_scope *);
static const char	*interpolate_with_scratch_scope(const struct match *,
    const struct macro_list *, const char *, struct arena_scope *,
    struct arena_scope *);
static ssize_t		 isbackref(const char *, struct backref *);

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
	LIST_INSERT_TAIL(ml, mh);

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

	while ((mh = LIST_FIRST(ml)) != NULL) {
		/* NOLINTNEXTLINE(clang-analyzer-core.NullDereference) */
		LIST_REMOVE(ml, mh);
		match_free(mh);
	}
}

int
matches_interpolate(struct match_list *ml, struct arena_scope *eternal_scope,
    struct arena *scratch)
{
	struct macro_list *macros;
	struct match *mh;
	int error = 0;

	arena_scope(scratch, s);

	macros = macros_alloc(MACRO_CTX_ACTION, &s);
	/* Construct action macro context. */
	macros_insertc(macros, "path",
	    message_get_path(LIST_FIRST(ml)->mh_msg));

	LIST_FOREACH(mh, ml) {
		if (match_interpolate(mh, macros, eternal_scope, scratch)) {
			error = 1;
			break;
		}
	}

	return error;
}

int
matches_exec(const struct match_list *ml, struct maildir *src,
    const struct environment *env, struct arena *scratch)
{
	struct maildir *dst = NULL;
	struct match *mh;
	int chsrc = 0;
	int error = 0;
	int rv = MATCH_EXEC_SUCCESS;

	arena_scope(scratch, s);

	LIST_FOREACH(mh, ml) {
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
			dst = maildir_open(mh->mh_path, 0, env, &s);
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
			rv = MATCH_EXEC_REJECTED;
			break;

		case EXPR_TYPE_EXEC: {
			unsigned int flags = mh->mh_expr->ex_exec.flags;
			int fd = -1;

			if (flags & EXPR_EXEC_STDIN) {
				fd = message_get_fd(msg,
				    flags & EXPR_EXEC_BODY);
				if (fd == -1) {
					error = 1;
					break;
				}
			}
			error = exec((char *const *)mh->mh_exec, fd);
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

	return error ? MATCH_EXEC_ERROR : rv;
}

int
matches_inspect(const struct match_list *ml, const struct environment *env,
    struct arena *scratch)
{
	const struct match *lhs, *mh;
	const struct message *msg;
	int dryrun = env->ev_options & OPTION_DRYRUN;

	arena_scope(scratch, s);

	lhs = LIST_FIRST(ml);
	msg = LIST_FIRST(ml)->mh_msg;

	LIST_FOREACH(mh, ml) {
		const struct expr *ex = mh->mh_expr;
		const struct match *rhs;
		const char *path;

		if ((ex->ex_flags & EXPR_FLAG_ACTION) == 0)
			continue;

		path = message_get_path(msg);
		log_info("%s -> %s\n",
		    env->ev_options & OPTION_STDIN ? "<stdin>" : path,
		    expr_inspect(ex, mh, msg, &s));

		if (!dryrun)
			continue;

		/* Handle all matches leading up to this action. */
		rhs = lhs;
		for (;;) {
			if (rhs == mh)
				break;
			expr_inspect_matches(rhs->mh_expr, rhs, env);
			rhs = LIST_NEXT(rhs);
		}
		lhs = rhs;
	}

	return dryrun;
}

struct match *
matches_find(struct match_list *ml, int type)
{
	struct match *mh;
	enum expr_type expr_type = (enum expr_type)type;

	LIST_FOREACH(mh, ml) {
		if (mh->mh_expr->ex_type == expr_type)
			return mh;
	}

	return NULL;
}

/*
 * Remove all expressions with the given type from the match list.
 * Returns the number of actions left in the match list.
 */
int
matches_remove(struct match_list *ml, int type)
{
	struct match *mh, *tmp;
	enum expr_type expr_type = (enum expr_type)type;
	int n = 0;

	LIST_FOREACH_SAFE(mh, ml, tmp) {
		const struct expr *ex = mh->mh_expr;

		if (ex->ex_type == expr_type) {
			LIST_REMOVE(ml, mh);
			match_free(mh);
		} else if (ex->ex_flags & EXPR_FLAG_ACTION) {
			n++;
		}
	}

	return n;
}

struct match *
match_alloc(struct expr *ex, struct message *msg, struct arena_scope *s)
{
	struct match *mh;

	mh = arena_calloc(s, 1, sizeof(*mh));
	mh->mh_expr = ex;
	mh->mh_msg = msg;
	return mh;
}

void
match_free(const struct match *mh)
{
	if (mh == NULL)
		return;
	arena_poison(mh, sizeof(*mh));
}

int
match_interpolate(struct match *mh, const struct macro_list *macros,
    struct arena_scope *eternal_scope, struct arena *scratch)
{
	struct message *msg = mh->mh_msg;

	switch (mh->mh_expr->ex_type) {
	case EXPR_TYPE_STAT:
	case EXPR_TYPE_MOVE: {
		const char *path;
		size_t n, siz;

		path = interpolate(mh, macros, scratch, mh->mh_path,
		    eternal_scope);
		if (path == NULL)
			return 1;
		siz = sizeof(mh->mh_path);
		n = strlcpy(mh->mh_path, path, siz);
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
		const char *buf, *label;

		arena_scope(scratch, scratch_scope);

		bf = arena_buffer_alloc(&scratch_scope, 128);

		labels = message_get_header(msg, "X-Label");
		if (labels != NULL) {
			size_t i;

			for (i = 0; i < VECTOR_LENGTH(labels); i++) {
				if (i > 0)
					buffer_putc(bf, ' ');
				buffer_printf(bf, "%s", labels[i]);
			}
		}
		LIST_FOREACH(str, mh->mh_expr->ex_strings) {
			if (buffer_get_len(bf) > 0)
				buffer_putc(bf, ' ');
			buffer_printf(bf, "%s", str->val);
		}
		buf = buffer_str(bf);
		label = interpolate_with_scratch_scope(mh, macros, buf,
		    eternal_scope, &scratch_scope);
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
		mh->mh_exec = arena_calloc(eternal_scope, len,
		    sizeof(*mh->mh_exec));
		mh->mh_nexec = len;
		LIST_FOREACH(str, mh->mh_expr->ex_strings) {
			const char *arg;

			arg = interpolate(mh, macros, scratch, str->val,
			    eternal_scope);
			if (arg == NULL)
				return 1;
			mh->mh_exec[nargs++] = arg;
		}
		break;
	}

	case EXPR_TYPE_ADD_HEADER: {
		const struct expr *ex = mh->mh_expr;
		const char *val;

		val = interpolate(mh, macros, scratch, ex->ex_add_header.val,
		    eternal_scope);
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
	dup = LIST_LAST(ml);
	if (dup != NULL && dup->mh_expr->ex_type == ex->ex_type) {
		LIST_REMOVE(ml, dup);
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

	LIST_REMOVE(ml, dup);
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
		tmp = LIST_PREV(tmp);
		if (tmp == NULL)
			return NULL;
		if (tmp->mh_expr->ex_type == EXPR_TYPE_MATCH)
			break;
	}

	for (;;) {
		tmp = LIST_NEXT(tmp);
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

static const char *
interpolate(const struct match *mh, const struct macro_list *macros,
    struct arena *scratch, const char *str, struct arena_scope *eternal_scope)
{
	arena_scope(scratch, scratch_scope);
	return interpolate_with_scratch_scope(mh, macros, str, eternal_scope,
	    &scratch_scope);
}

static const char *
interpolate_with_scratch_scope(const struct match *mh,
    const struct macro_list *macros, const char *str,
    struct arena_scope *eternal_scope, struct arena_scope *scratch_scope)
{
	struct buffer *bf;
	size_t i = 0;

	bf = arena_buffer_alloc(eternal_scope, 64);

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

		n = ismacro(&str[i], &macro, scratch_scope);
		if (n < 0)
			goto mcerr;
		if (n > 0) {
			const struct macro *mc;

			mc = macros_find(macros, macro);
			if (mc == NULL)
				goto mcerr;

			buffer_printf(bf, "%s", macro_get_value(mc));
			i += (size_t)n;
			continue;
		}

		buffer_putc(bf, str[i++]);
	}

	return buffer_str(bf);

brerr:
	warnx("%s: invalid back-reference", str);
	return NULL;
mcerr:
	warnx("%s: invalid macro", str);
	return NULL;
}
