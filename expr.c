#include "config.h"

#include <sys/stat.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

#define EXPR_EVAL_ARGS	struct expr *, struct match_list *,	\
	struct message *, const struct environment *

static int expr_eval_all(EXPR_EVAL_ARGS);
static int expr_eval_and(EXPR_EVAL_ARGS);
static int expr_eval_attachment(EXPR_EVAL_ARGS);
static int expr_eval_attachment_body(EXPR_EVAL_ARGS);
static int expr_eval_attachment_header(EXPR_EVAL_ARGS);
static int expr_eval_block(EXPR_EVAL_ARGS);
static int expr_eval_body(EXPR_EVAL_ARGS);
static int expr_eval_break(EXPR_EVAL_ARGS);
static int expr_eval_date(EXPR_EVAL_ARGS);
static int expr_eval_discard(EXPR_EVAL_ARGS);
static int expr_eval_flag(EXPR_EVAL_ARGS);
static int expr_eval_header(EXPR_EVAL_ARGS);
static int expr_eval_label(EXPR_EVAL_ARGS);
static int expr_eval_move(EXPR_EVAL_ARGS);
static int expr_eval_neg(EXPR_EVAL_ARGS);
static int expr_eval_new(EXPR_EVAL_ARGS);
static int expr_eval_old(EXPR_EVAL_ARGS);
static int expr_eval_or(EXPR_EVAL_ARGS);
static int expr_eval_pass(EXPR_EVAL_ARGS);
static int expr_eval_reject(EXPR_EVAL_ARGS);

static int expr_inspect_prefix(const struct expr *, FILE *,
    const struct environment *);
static int expr_regexec(struct expr *, const char *, const char *, int);

static size_t append(char **, size_t *, size_t *, const char *);

/*
 * Allocate a new expression with the given type.
 *
 * The caller is responsible for freeing the returned memory using
 * expr_free().
 */
struct expr *
expr_alloc(enum expr_type type, int lno, struct expr *lhs, struct expr *rhs)
{
	struct expr *ex;

	ex = calloc(1, sizeof(*ex));
	if (ex == NULL)
		err(1, NULL);
	ex->ex_type = type;
	ex->ex_lno = lno;
	ex->ex_lhs = lhs;
	ex->ex_rhs = rhs;
	switch (ex->ex_type) {
	case EXPR_TYPE_BLOCK:
		ex->ex_eval = &expr_eval_block;
		break;
	case EXPR_TYPE_AND:
		ex->ex_eval = &expr_eval_and;
		break;
	case EXPR_TYPE_OR:
		ex->ex_eval = &expr_eval_or;
		break;
	case EXPR_TYPE_NEG:
		ex->ex_eval = &expr_eval_neg;
		break;
	case EXPR_TYPE_ALL:
		ex->ex_eval = &expr_eval_all;
		break;
	case EXPR_TYPE_ATTACHMENT:
		ex->ex_eval = &expr_eval_attachment;
		break;
	case EXPR_TYPE_ATTACHMENT_BODY:
		ex->ex_eval = &expr_eval_attachment_body;
		ex->ex_flags = EXPR_FLAG_INSPECT | EXPR_FLAG_MATCH |
		    EXPR_FLAG_INTERPOLATE;
		break;
	case EXPR_TYPE_ATTACHMENT_HEADER:
		ex->ex_eval = &expr_eval_attachment_header;
		ex->ex_flags = EXPR_FLAG_INSPECT | EXPR_FLAG_MATCH |
		    EXPR_FLAG_INTERPOLATE;
		break;
	case EXPR_TYPE_BODY:
		ex->ex_eval = &expr_eval_body;
		ex->ex_flags = EXPR_FLAG_INSPECT | EXPR_FLAG_MATCH |
		    EXPR_FLAG_INTERPOLATE;
		break;
	case EXPR_TYPE_DATE:
		ex->ex_eval = &expr_eval_date;
		ex->ex_flags = EXPR_FLAG_INSPECT | EXPR_FLAG_MATCH;
		break;
	case EXPR_TYPE_HEADER:
		ex->ex_eval = &expr_eval_header;
		ex->ex_flags = EXPR_FLAG_INSPECT | EXPR_FLAG_MATCH |
		    EXPR_FLAG_INTERPOLATE;
		break;
	case EXPR_TYPE_NEW:
		ex->ex_eval = &expr_eval_new;
		break;
	case EXPR_TYPE_OLD:
		ex->ex_eval = &expr_eval_old;
		break;
	case EXPR_TYPE_MOVE:
		ex->ex_eval = &expr_eval_move;
		ex->ex_flags = EXPR_FLAG_ACTION | EXPR_FLAG_MATCH |
		    EXPR_FLAG_PATH;
		break;
	case EXPR_TYPE_FLAG:
		ex->ex_eval = &expr_eval_flag;
		ex->ex_flags = EXPR_FLAG_ACTION | EXPR_FLAG_MATCH |
		    EXPR_FLAG_PATH;
		break;
	case EXPR_TYPE_DISCARD:
		ex->ex_eval = &expr_eval_discard;
		ex->ex_flags = EXPR_FLAG_ACTION | EXPR_FLAG_MATCH;
		break;
	case EXPR_TYPE_BREAK:
		ex->ex_eval = &expr_eval_break;
		ex->ex_flags = EXPR_FLAG_ACTION | EXPR_FLAG_MATCH;
		break;
	case EXPR_TYPE_LABEL:
		ex->ex_eval = &expr_eval_label;
		ex->ex_flags = EXPR_FLAG_ACTION | EXPR_FLAG_MATCH |
		    EXPR_FLAG_PATH;
		break;
	case EXPR_TYPE_PASS:
		ex->ex_eval = &expr_eval_pass;
		ex->ex_flags = EXPR_FLAG_ACTION | EXPR_FLAG_MATCH;
		break;
	case EXPR_TYPE_REJECT:
		ex->ex_eval = &expr_eval_reject;
		ex->ex_flags = EXPR_FLAG_ACTION | EXPR_FLAG_MATCH;
		break;
	}

	if (ex->ex_flags & EXPR_FLAG_MATCH) {
		ex->ex_match = calloc(1, sizeof(*ex->ex_match));
		if (ex->ex_match == NULL)
			err(1, NULL);
		ex->ex_match->mh_expr = ex;
	}

	return ex;
}

void
expr_free(struct expr *ex)
{

	if (ex == NULL)
		return;

	expr_free(ex->ex_lhs);
	expr_free(ex->ex_rhs);
	strings_free(ex->ex_strings);
	match_reset(ex->ex_match);
	regfree(&ex->ex_re.r_pattern);
	free(ex->ex_re.r_matches);
	free(ex->ex_match);
	free(ex);
}

void
expr_set_date(struct expr *ex, enum expr_date_field field,
    enum expr_date_cmp cmp, time_t age)
{

	assert(ex->ex_type == EXPR_TYPE_DATE);

	ex->ex_date.d_field = field;
	ex->ex_date.d_cmp = cmp;
	ex->ex_date.d_age = age;

	/* Cheat a bit by adding a match all pattern used during inspect. */
	(void)expr_set_pattern(ex, ".*", 0, NULL);
}

void
expr_set_strings(struct expr *ex, struct string_list *strings)
{

	ex->ex_strings = strings;
}

/*
 * Associate the given pattern with the expression.
 *
 * The flags may be any combination of the following values:
 *
 *     EXPR_PATTERN_ICASE    Ignore case.
 *
 *     EXPR_PATTERN_FORCE    Force usage of matches belonging to the
 *                           given pattern during interpolation.
 *
 *     EXPR_PATTERN_LCASE    Lowercase the matched string from a subexpression
 *                           before interpolation.
 *
 *     EXPR_PATTERN_UCASE    Uppercase the matched string from a subexpression
 *                           before interpolation.
 *
 * Returns zero if pattern is successfully compiled into a regular expression.
 * Otherwise, returns non-zero and if errstr is not NULL it will point to an
 * explanation on why the compilation failed.
 */
int
expr_set_pattern(struct expr *ex, const char *pattern, unsigned int flags,
    const char **errstr)
{
	int error;
	int rflags = REG_EXTENDED | REG_NEWLINE;

	assert(ex->ex_re.r_nmatches == 0);

	if (flags & EXPR_PATTERN_ICASE) {
		rflags |= REG_ICASE;
		flags &= ~EXPR_PATTERN_ICASE;
	}
	if (flags & EXPR_PATTERN_FORCE) {
		ex->ex_re.r_flags |= EXPR_PATTERN_FORCE;
		flags &= ~EXPR_PATTERN_FORCE;
	}
	if (flags & EXPR_PATTERN_LCASE) {
		ex->ex_re.r_flags |= EXPR_PATTERN_LCASE;
		flags &= ~EXPR_PATTERN_LCASE;
	}
	if (flags & EXPR_PATTERN_UCASE) {
		ex->ex_re.r_flags |= EXPR_PATTERN_UCASE;
		flags &= ~EXPR_PATTERN_UCASE;
	}
	assert(flags == 0);

	if ((error = regcomp(&ex->ex_re.r_pattern, pattern, rflags)) != 0) {
		if (errstr != NULL) {
			static char buf[1024];

			regerror(error, &ex->ex_re.r_pattern, buf, sizeof(buf));
			*errstr = buf;
		}
		return 1;
	}
	ex->ex_re.r_nmatches = ex->ex_re.r_pattern.re_nsub + 1;
	ex->ex_re.r_matches = reallocarray(NULL, ex->ex_re.r_nmatches,
	    sizeof(*ex->ex_re.r_matches));
	if (ex->ex_re.r_matches == NULL)
		err(1, NULL);

	return 0;
}

/*
 * Returns 0 if the expression matches the given message. The given match list
 * will be populated with the matching expressions.
 * Otherwise, non-zero is returned.
 */
int
expr_eval(struct expr *ex, struct match_list *ml, struct message *msg,
    const struct environment *env)
{

	return ex->ex_eval(ex, ml, msg, env);
}

/*
 * Returns the number of expressions with the given type.
 */
int
expr_count(const struct expr *ex, enum expr_type type)
{
	int n = 0;

	if (ex == NULL)
		return 0;

	if (ex->ex_type == type)
		n = 1;
	return n + expr_count(ex->ex_lhs, type) + expr_count(ex->ex_rhs, type);
}

/*
 * Returns the number of actions.
 */
int
expr_count_actions(const struct expr *ex)
{
	int n = 0;

	if (ex == NULL)
		return 0;

	if (ex->ex_flags & EXPR_FLAG_ACTION)
		n = 1;
	return n + expr_count_actions(ex->ex_lhs) +
	    expr_count_actions(ex->ex_rhs);
}

/*
 * Returns the number of expressions with the given pattern flags.
 */
int
expr_count_patterns(const struct expr *ex, unsigned int flags)
{
	int n = 0;

	if (ex == NULL)
		return 0;

	if (ex->ex_re.r_nmatches > 0 && (ex->ex_re.r_flags & flags))
		n = 1;
	return n + expr_count_patterns(ex->ex_lhs, flags) +
	    expr_count_patterns(ex->ex_rhs, flags);
}

/*
 * Writes a human readable representation of the latest match to fh.
 */
void
expr_inspect(const struct expr *ex, FILE *fh, const struct environment *env)
{
	const struct match *match;
	const char *lbeg, *lend, *p;
	unsigned int i;
	int beg, end, len, indent, pindent;
	int printkey = 1;

	if ((ex->ex_flags & EXPR_FLAG_INSPECT) == 0)
		return;

	match = ex->ex_match;
	pindent = strlen(match->mh_key) + 2;

	for (i = 0; i < ex->ex_re.r_nmatches; i++) {
		beg = ex->ex_re.r_matches[i].rm_so;
		end = ex->ex_re.r_matches[i].rm_eo;

		lbeg = match->mh_val;
		for (;;) {
			if ((p = strchr(lbeg, '\n')) == NULL ||
			    p > match->mh_val + beg)
				break;
			lbeg = p + 1;
		}
		lbeg += nspaces(lbeg);
		lend = strchr(lbeg, '\n');
		if (lend == NULL)
			lend = match->mh_val + strlen(match->mh_val);

		len = end - beg;
		/* Try to compensate for the "^$" markers. */
		if (len >= 2)
			len -= 2;
		else
			len = 0;

		if (printkey) {
			pindent += expr_inspect_prefix(ex, fh, env);
			printkey = 0;
			fprintf(fh, "%s: ", match->mh_key);
		} else {
			fprintf(fh, "%*s", pindent, "");
		}
		indent = beg - (lbeg - match->mh_val) + pindent;
		fprintf(fh, "%.*s\n%*s^%*s$\n",
		    (int)(lend - lbeg), lbeg, indent, "", len, "");
	}
}

static int
expr_eval_all(struct expr *UNUSED(ex), struct match_list *UNUSED(ml),
    struct message *UNUSED(msg), const struct environment *UNUSED(env))
{

	return EXPR_MATCH;
}

static int
expr_eval_and(struct expr *ex, struct match_list *ml, struct message *msg,
    const struct environment *env)
{
	int ev;

	if ((ev = expr_eval(ex->ex_lhs, ml, msg, env)) != EXPR_MATCH)
		return ev; /* no match or error, short-circuit */
	return expr_eval(ex->ex_rhs, ml, msg, env);
}

static int
expr_eval_attachment(struct expr *UNUSED(ex), struct match_list *UNUSED(ml),
    struct message *msg, const struct environment *UNUSED(env))
{
	struct message_list *attachments;
	int ev;

	if (message_get_attachments(msg, &attachments))
		return EXPR_ERROR;

	/* Presence of attachments is considered a match. */
	ev = TAILQ_EMPTY(attachments) ? EXPR_NOMATCH : EXPR_MATCH;
	message_list_free(attachments);
	return ev;
}

static int
expr_eval_attachment_body(struct expr *ex, struct match_list *ml,
    struct message *msg, const struct environment *env)
{
	struct message_list *attachments;
	struct message *attach;

	if (message_get_attachments(msg, &attachments))
		return EXPR_ERROR;

	TAILQ_FOREACH(attach, attachments, me_entry) {
		int ev = expr_eval_body(ex, ml, attach, env);

		if (ev == EXPR_NOMATCH)
			continue;

		message_list_free(attachments);
		return ev;
	}

	message_list_free(attachments);
	return EXPR_NOMATCH;
}

static int
expr_eval_attachment_header(struct expr *ex, struct match_list *ml,
    struct message *msg, const struct environment *env)
{
	struct message_list *attachments;
	struct message *attach;

	if (message_get_attachments(msg, &attachments))
		return EXPR_ERROR;

	TAILQ_FOREACH(attach, attachments, me_entry) {
		int ev = expr_eval_header(ex, ml, attach, env);

		if (ev == EXPR_NOMATCH)
			continue;

		message_list_free(attachments);
		return ev;
	}

	message_list_free(attachments);
	return EXPR_NOMATCH;
}

static int
expr_eval_block(struct expr *ex, struct match_list *ml,
    struct message *msg, const struct environment *env)
{
	int ev;

	ev = expr_eval(ex->ex_lhs, ml, msg, env);
	if (ev == EXPR_ERROR)
		return EXPR_ERROR;

	if (matches_find(ml, EXPR_TYPE_BREAK) != NULL) {
		matches_clear(ml);
		return EXPR_NOMATCH; /* break, continue evaluation */
	}

	if (matches_find(ml, EXPR_TYPE_PASS) != NULL) {
		/*
		 * If removing the pass action results in a match list without
		 * any actions left, we got a pass followed by no effective
		 * action. Therefore treat it as a no match.
		 */
		if (matches_remove(ml, EXPR_TYPE_PASS) > 0)
			return EXPR_MATCH;
		else
			return EXPR_NOMATCH;
	}

	return ev;
}

static int
expr_eval_body(struct expr *ex, struct match_list *ml,
    struct message *msg, const struct environment *env)
{
	const char *body;
	int ev;

	body = message_get_body(msg);
	if (body == NULL)
		return EXPR_ERROR;

	ev = expr_regexec(ex, "Body", body, env->ev_options & OPTION_DRYRUN);
	if (ev != EXPR_MATCH)
		return ev;

	if (matches_append(ml, ex->ex_match, msg))
		return EXPR_ERROR;
	return EXPR_MATCH;

}

static int
expr_eval_break(struct expr *ex, struct match_list *ml, struct message *msg,
    const struct environment *UNUSED(env))
{

	if (matches_append(ml, ex->ex_match, msg))
		return EXPR_ERROR;

	/*
	 * Return match in order to continue evaluation. The return value is
	 * later inverted by expr_eval_block().
	 */
	return EXPR_MATCH;
}

static int
expr_eval_date(struct expr *ex, struct match_list *ml,
    struct message *msg, const struct environment *env)
{
	char buf[32];
	const char *date;
	time_t delta, tim;
	int ev;

	if (ex->ex_date.d_field == EXPR_DATE_FIELD_HEADER) {
		date = message_get_header1(msg, "Date");
		if (date == NULL)
			return EXPR_NOMATCH;
		if (time_parse(date, &tim, env))
			return EXPR_ERROR;
	} else {
		struct stat st;
		/*
		 * Initial value might look redundant but otherwise GCC will
		 * complain about it might being used uninitialized.
		 */
		struct timespec *ts = NULL;

		switch (ex->ex_date.d_field) {
		case EXPR_DATE_FIELD_HEADER:
			return EXPR_ERROR; /* UNREACHABLE */
		case EXPR_DATE_FIELD_ACCESS:
			ts = &st.st_atim;
			break;
		case EXPR_DATE_FIELD_MODIFIED:
			ts = &st.st_mtim;
			break;
		case EXPR_DATE_FIELD_CREATED:
			ts = &st.st_ctim;
			break;
		}
		if (stat(msg->me_path, &st) == -1) {
			warn("stat: %s", msg->me_path);
			return EXPR_ERROR;
		}
		tim = ts->tv_sec;
		date = time_format(tim, buf, sizeof(buf));
		if (date == NULL)
			return EXPR_ERROR;
	}

	delta = env->ev_now - tim;
	switch (ex->ex_date.d_cmp) {
	case EXPR_DATE_CMP_LT:
		if (!(delta < ex->ex_date.d_age))
			return EXPR_NOMATCH;
		break;
	case EXPR_DATE_CMP_GT:
		if (!(delta > ex->ex_date.d_age))
			return EXPR_NOMATCH;
		break;
	}

	/* Populate matches, only used during dry run. */
	ev = expr_regexec(ex, "Date", date, env->ev_options & OPTION_DRYRUN);
	if (ev != EXPR_MATCH)
		return ev;

	if (matches_append(ml, ex->ex_match, msg))
		return EXPR_ERROR;
	return EXPR_MATCH;
}

static int
expr_eval_discard(struct expr *ex, struct match_list *ml, struct message *msg,
    const struct environment *UNUSED(env))
{
	struct match *mh = ex->ex_match;
	size_t siz;

	/* Populate the path in case of a dry run. */
	siz = sizeof(mh->mh_path);
	if (strlcpy(mh->mh_path, "<discard>", siz) >= siz) {
		warnc(ENAMETOOLONG, "%s", __func__);
		return EXPR_ERROR;
	}

	if (matches_append(ml, mh, msg))
		return EXPR_ERROR;

	return EXPR_MATCH;
}

static int
expr_eval_flag(struct expr *ex, struct match_list *ml, struct message *msg,
    const struct environment *UNUSED(env))
{
	struct match *mh = ex->ex_match;
	const char *subdir;
	size_t siz;

	subdir = TAILQ_FIRST(ex->ex_strings)->val;
	siz = sizeof(mh->mh_subdir);
	if (strlcpy(mh->mh_subdir, subdir, siz) >= siz) {
		warnc(ENAMETOOLONG, "%s", __func__);
		return EXPR_ERROR;
	}

	if (matches_append(ml, mh, msg))
		return EXPR_ERROR;

	return EXPR_MATCH;
}

static int
expr_eval_header(struct expr *ex, struct match_list *ml,
    struct message *msg, const struct environment *env)
{
	const struct string_list *values;
	const struct string *key, *val;

	TAILQ_FOREACH(key, ex->ex_strings, entry) {
		values = message_get_header(msg, key->val);
		if (values == NULL)
			continue;

		TAILQ_FOREACH(val, values, entry) {
			int ev = expr_regexec(ex, key->val, val->val,
			    env->ev_options & OPTION_DRYRUN);

			if (ev == EXPR_NOMATCH)
				continue;
			if (ev == EXPR_ERROR)
				return EXPR_ERROR;

			if (matches_append(ml, ex->ex_match, msg))
				return EXPR_ERROR;
			return ev;
		}
	}
	return EXPR_NOMATCH;
}

static int
expr_eval_label(struct expr *ex, struct match_list *ml, struct message *msg,
    const struct environment *UNUSED(env))
{
	const struct string_list *labels;
	const struct string *str;
	char *buf = NULL;
	size_t buflen = 0;
	size_t bufsiz = 0;

	labels = message_get_header(msg, "X-Label");
	if (labels != NULL) {
		TAILQ_FOREACH(str, labels, entry) {
			/* The header can be empty. */
			if (append(&buf, &bufsiz, &buflen, str->val) > 0)
				append(&buf, &bufsiz, &buflen, " ");
		}
	}

	TAILQ_FOREACH(str, ex->ex_strings, entry) {
		/* The label cannot be empty, enforced by the config parser. */
		append(&buf, &bufsiz, &buflen, str->val);
		append(&buf, &bufsiz, &buflen, " ");
	}
	/* NUL-terminate and remove trailing space added above. */
	if (buflen > 0)
		buf[buflen - 1] = '\0';

	message_set_header(msg, "X-Label", buf);

	if (matches_append(ml, ex->ex_match, msg))
		return EXPR_ERROR;

	return EXPR_MATCH;
}

static int
expr_eval_move(struct expr *ex, struct match_list *ml, struct message *msg,
    const struct environment *UNUSED(env))
{
	struct match *mh = ex->ex_match;
	const char *maildir;
	size_t siz;

	maildir = TAILQ_FIRST(ex->ex_strings)->val;
	siz = sizeof(mh->mh_maildir);
	if (strlcpy(mh->mh_maildir, maildir, siz) >= siz) {
		warnc(ENAMETOOLONG, "%s", __func__);
		return EXPR_ERROR;
	}

	if (matches_append(ml, mh, msg))
		return EXPR_ERROR;

	return EXPR_MATCH;
}

static int
expr_eval_neg(struct expr *ex, struct match_list *ml, struct message *msg,
    const struct environment *env)
{

	assert(ex->ex_rhs == NULL);

	switch (expr_eval(ex->ex_lhs, ml, msg, env)) {
	case EXPR_ERROR:
		return EXPR_ERROR;
	case EXPR_NOMATCH:
		return EXPR_MATCH;
	}

	/* No match, invalidate match below current expression. */
	matches_clear(ml);
	return EXPR_NOMATCH;
}

static int
expr_eval_new(struct expr *UNUSED(ex), struct match_list *UNUSED(ml),
    struct message *msg, const struct environment *UNUSED(env))
{
	char buf[NAME_MAX + 1];

	if (pathslice(msg->me_path, buf, sizeof(buf), -2, -2) == NULL ||
	    strcmp(buf, "new"))
		return EXPR_NOMATCH;
	return EXPR_MATCH;
}

static int
expr_eval_old(struct expr *UNUSED(ex), struct match_list *UNUSED(ml),
    struct message *msg, const struct environment *UNUSED(env))
{
	char buf[NAME_MAX + 1];

	if (message_flags_isset(&msg->me_flags, 'S'))
		return EXPR_NOMATCH;
	if (pathslice(msg->me_path, buf, sizeof(buf), -2, -2) == NULL ||
	    strcmp(buf, "cur"))
		return EXPR_NOMATCH;
	return EXPR_MATCH;
}

static int
expr_eval_or(struct expr *ex, struct match_list *ml, struct message *msg,
    const struct environment *env)
{
	int ev;

	if ((ev = expr_eval(ex->ex_lhs, ml, msg, env)) != EXPR_NOMATCH)
		return ev; /* match or error, short-circuit */
	return expr_eval(ex->ex_rhs, ml, msg, env);
}

static int
expr_eval_pass(struct expr *ex, struct match_list *ml, struct message *msg,
    const struct environment *UNUSED(env))
{

	if (matches_append(ml, ex->ex_match, msg))
		return EXPR_ERROR;

	/*
	 * Return no match in order stop evaluation and move on to the next
	 * expression within the same block. The return value is later inverted
	 * by expr_eval_block() in order to not continue evaluation outside of
	 * the current block.
	 */
	return EXPR_NOMATCH;
}

static int
expr_eval_reject(struct expr *ex, struct match_list *ml, struct message *msg,
    const struct environment *UNUSED(env))
{
	struct match *mh = ex->ex_match;
	size_t siz;

	/* Populate the path in case of a dry run. */
	siz = sizeof(mh->mh_path);
	if (strlcpy(mh->mh_path, "<reject>", siz) >= siz) {
		warnc(ENAMETOOLONG, "%s", __func__);
		return EXPR_ERROR;
	}

	if (matches_append(ml, mh, msg))
		return EXPR_ERROR;

	return EXPR_MATCH;
}

static int
expr_inspect_prefix(const struct expr *ex, FILE *fh,
    const struct environment *env)
{
	const char *path;
	size_t len;
	int n;
	int nwrite = 0;

	path = env->ev_confpath;
	len = strlen(env->ev_home);
	if (strncmp(path, env->ev_home, len) == 0) {
		n = fprintf(fh, "~");
		if (n > 0)
			nwrite += n;
		path += len;
	}
	n = fprintf(fh, "%s:%d: ", path, ex->ex_lno);
	if (n > 0)
		nwrite += n;
	return nwrite;
}

static int
expr_regexec(struct expr *ex, const char *key, const char *val, int dryrun)
{
	int error;

	error = regexec(&ex->ex_re.r_pattern, val, ex->ex_re.r_nmatches,
	    ex->ex_re.r_matches, 0);
	if (error == REG_NOMATCH)
		return EXPR_NOMATCH;
	if (error != 0)
		return EXPR_ERROR;

	match_copy(ex->ex_match, val, ex->ex_re.r_matches,
	    ex->ex_re.r_nmatches);

	if (dryrun) {
		ex->ex_match->mh_key = strdup(key);
		if (ex->ex_match->mh_key == NULL)
			err(1, NULL);
		ex->ex_match->mh_val = strdup(val);
		if (ex->ex_match->mh_val == NULL)
			err(1, NULL);
	}

	return EXPR_MATCH;
}

/*
 * Append str to buf, both bufsiz and buflen are updated accordingly.
 * Returns the number of appended bytes.
 */
static size_t
append(char **buf, size_t *bufsiz, size_t *buflen, const char *str)
{
	size_t len, newsiz;

	len = strlen(str);
	while (*buflen + len >= *bufsiz) {
		newsiz = 2 * *bufsiz;
		if (newsiz == 0)
			newsiz = 128;
		*buf = realloc(*buf, newsiz);
		if (*buf == NULL)
			err(1, NULL);
		*bufsiz = newsiz;
	}
	memcpy(*buf + *buflen, str, len);
	*buflen += len;

	return len;
}
