#include "config.h"

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

/*
 * Flags used to denote properties of expression types.
 */
#define EXPR_FLAG_ACTION	0x00000001
#define EXPR_FLAG_INSPECT	0x00000002
#define EXPR_FLAG_MATCH		0x00000004

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

static unsigned int expr_flags(const struct expr *);
static int expr_inspect_prefix(const struct expr *, FILE *,
    const struct environment *);
static int expr_regexec(struct expr *, struct match_list *, const char *,
    const char *, int);

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
	ex->type = type;
	ex->lno = lno;
	ex->lhs = lhs;
	ex->rhs = rhs;

	if (expr_flags(ex) & EXPR_FLAG_MATCH) {
		ex->match = calloc(1, sizeof(*ex->match));
		if (ex->match == NULL)
			err(1, NULL);
		ex->match->mh_expr = ex;
	}

	return ex;
}

void
expr_free(struct expr *ex)
{
	if (ex == NULL)
		return;

	expr_free(ex->lhs);
	expr_free(ex->rhs);
	strings_free(ex->strings);
	match_reset(ex->match);
	regfree(&ex->ex_re.r_pattern);
	free(ex->ex_re.r_matches);
	free(ex->match);
	free(ex);
}

void
expr_set_date(struct expr *ex, enum expr_cmp cmp, time_t age)
{
	assert(ex->type == EXPR_TYPE_DATE);

	ex->date.cmp = cmp;
	ex->date.age = age;

	/* Cheat a bit by adding a match all pattern used during inspect. */
	(void)expr_set_pattern(ex, ".*", 0, NULL);
}

void
expr_set_strings(struct expr *ex, struct string_list *strings)
{
	ex->strings = strings;
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
 * Returns zero if pattern is successfully compiled into a regular expression.
 * Otherwise, returns non-zero and if errstr is not NULL it will point to an
 * explanation on why the compilation failed.
 */
int
expr_set_pattern(struct expr *ex, const char *pattern, unsigned int flags,
    const char **errstr)
{
	static char buf[1024];
	int ret;
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
	assert(flags == 0);

	if ((ret = regcomp(&ex->ex_re.r_pattern, pattern, rflags)) != 0) {
		if (errstr != NULL) {
			regerror(ret, &ex->ex_re.r_pattern, buf, sizeof(buf));
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
	switch (ex->type) {
	case EXPR_TYPE_BLOCK:
		return expr_eval_block(ex, ml, msg, env);
	case EXPR_TYPE_AND:
		return expr_eval_and(ex, ml, msg, env);
	case EXPR_TYPE_OR:
		return expr_eval_or(ex, ml, msg, env);
	case EXPR_TYPE_NEG:
		return expr_eval_neg(ex, ml, msg, env);
	case EXPR_TYPE_ATTACHMENT:
		return expr_eval_attachment(ex, ml, msg, env);
	case EXPR_TYPE_ATTACHMENT_BODY:
		return expr_eval_attachment_body(ex, ml, msg, env);
	case EXPR_TYPE_ATTACHMENT_HEADER:
		return expr_eval_attachment_header(ex, ml, msg, env);
	case EXPR_TYPE_ALL:
		return expr_eval_all(ex, ml, msg, env);
	case EXPR_TYPE_BODY:
		return expr_eval_body(ex, ml, msg, env);
	case EXPR_TYPE_DATE:
		return expr_eval_date(ex, ml, msg, env);
	case EXPR_TYPE_HEADER:
		return expr_eval_header(ex, ml, msg, env);
	case EXPR_TYPE_NEW:
		return expr_eval_new(ex, ml, msg, env);
	case EXPR_TYPE_OLD:
		return expr_eval_old(ex, ml, msg, env);
	case EXPR_TYPE_MOVE:
		return expr_eval_move(ex, ml, msg, env);
	case EXPR_TYPE_FLAG:
		return expr_eval_flag(ex, ml, msg, env);
	case EXPR_TYPE_DISCARD:
		return expr_eval_discard(ex, ml, msg, env);
	case EXPR_TYPE_BREAK:
		return expr_eval_break(ex, ml, msg, env);
	case EXPR_TYPE_LABEL:
		return expr_eval_label(ex, ml, msg, env);
	case EXPR_TYPE_PASS:
		return expr_eval_pass(ex, ml, msg, env);
	case EXPR_TYPE_REJECT:
		return expr_eval_reject(ex, ml, msg, env);
	}

	return EXPR_NOMATCH;
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

	if (ex->type == type)
		n = 1;
	return n + expr_count(ex->lhs, type) + expr_count(ex->rhs, type);
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

	if (expr_flags(ex) & EXPR_FLAG_ACTION)
		n = 1;
	return n + expr_count_actions(ex->lhs) + expr_count_actions(ex->rhs);
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
	return n + expr_count_patterns(ex->lhs, flags) +
	    expr_count_patterns(ex->rhs, flags);
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

	if ((expr_flags(ex) & EXPR_FLAG_INSPECT) == 0)
		return;

	match = ex->match;
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
		if (len >= 2)
			len -= 2;

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
	int e;

	if ((e = expr_eval(ex->lhs, ml, msg, env)))
		return e; /* no match or error, short-circuit */
	return expr_eval(ex->rhs, ml, msg, env);
}

static int
expr_eval_attachment(struct expr *UNUSED(ex), struct match_list *UNUSED(ml),
    struct message *msg, const struct environment *UNUSED(env))
{
	struct message_list *attachments;

	attachments = message_get_attachments(msg);
	if (attachments == NULL)
		return EXPR_NOMATCH;

	/* Presence of attachments is considered a match. */
	message_list_free(attachments);
	return EXPR_MATCH;
}

static int
expr_eval_attachment_body(struct expr *ex, struct match_list *ml,
    struct message *msg, const struct environment *env)
{
	struct message_list *attachments;
	struct message *attach;

	attachments = message_get_attachments(msg);
	if (attachments == NULL)
		return EXPR_NOMATCH;

	TAILQ_FOREACH(attach, attachments, me_entry) {
		if (expr_eval_body(ex, ml, attach, env))
			continue;

		message_list_free(attachments);
		return EXPR_MATCH;
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

	attachments = message_get_attachments(msg);
	if (attachments == NULL)
		return EXPR_NOMATCH;

	TAILQ_FOREACH(attach, attachments, me_entry) {
		if (expr_eval_header(ex, ml, attach, env))
			continue;

		message_list_free(attachments);
		return EXPR_MATCH;
	}

	message_list_free(attachments);
	return EXPR_NOMATCH;
}

static int
expr_eval_block(struct expr *ex, struct match_list *ml,
    struct message *msg, const struct environment *env)
{
	struct match *mh;
	int e;

	e = expr_eval(ex->lhs, ml, msg, env);
	if (e == EXPR_ERROR)
		return EXPR_ERROR;

	if (matches_find(ml, EXPR_TYPE_BREAK) != NULL) {
		matches_clear(ml);
		return EXPR_NOMATCH; /* break, continue evaluation */
	}

	mh = matches_find(ml, EXPR_TYPE_PASS);
	if (mh != NULL) {
		matches_remove(ml, mh);
		return EXPR_MATCH; /* pass, stop evaluation */
	}

	return e;
}

static int
expr_eval_body(struct expr *ex, struct match_list *ml,
    struct message *msg, const struct environment *env)
{
	if (expr_regexec(ex, ml, "Body", msg->me_body,
		    env->ev_options & OPTION_DRYRUN))
		return EXPR_NOMATCH;
	return EXPR_MATCH;
}

static int
expr_eval_break(struct expr *ex, struct match_list *ml,
    struct message *UNUSED(msg), const struct environment *UNUSED(env))
{
	matches_append(ml, ex->match);
	return EXPR_MATCH;
}

static int
expr_eval_date(struct expr *ex, struct match_list *ml,
    struct message *msg, const struct environment *env)
{
	const char *date;
	time_t delta, tim;

	date = message_get_header1(msg, "Date");
	if (date == NULL)
		return EXPR_NOMATCH;
	if (time_parse(date, &tim, env))
		return EXPR_ERROR;

	delta = env->ev_now - tim;
	switch (ex->date.cmp) {
	case EXPR_CMP_LT:
		if (!(delta < ex->date.age))
			return EXPR_NOMATCH;
		break;
	case EXPR_CMP_GT:
		if (!(delta > ex->date.age))
			return EXPR_NOMATCH;
		break;
	}

	if (env->ev_options & OPTION_DRYRUN) {
		match_reset(ex->match);

		ex->match->mh_key = strdup("Date");
		if (ex->match->mh_key == NULL)
			err(1, NULL);
		ex->match->mh_val = strdup(date);
		if (ex->match->mh_val == NULL)
			err(1, NULL);
	}

	(void)expr_regexec(ex, ml, "Date", date,
	    env->ev_options & OPTION_DRYRUN);

	return EXPR_MATCH;
}

static int
expr_eval_discard(struct expr *ex, struct match_list *ml,
    struct message *UNUSED(msg), const struct environment *UNUSED(env))
{
	size_t len;

	matches_append(ml, ex->match);

	len = sizeof(ml->ml_path);
	if (strlcpy(ml->ml_path, "<discard>", len) >= len)
		errc(1, ENAMETOOLONG, "%s", __func__);

	return EXPR_MATCH;
}

static int
expr_eval_flag(struct expr *ex, struct match_list *ml,
    struct message *UNUSED(msg), const struct environment *UNUSED(env))
{
	const struct string *str;
	size_t len;

	str = TAILQ_FIRST(ex->strings);
	len = sizeof(ml->ml_subdir);
	if (strlcpy(ml->ml_subdir, str->val, len) >= len)
		errc(1, ENAMETOOLONG, "%s", __func__);

	matches_append(ml, ex->match);

	return EXPR_MATCH;
}

static int
expr_eval_header(struct expr *ex, struct match_list *ml,
    struct message *msg, const struct environment *env)
{
	const struct string_list *values;
	const struct string *key, *val;

	TAILQ_FOREACH(key, ex->strings, entry) {
		values = message_get_header(msg, key->val);
		if (values == NULL)
			continue;

		TAILQ_FOREACH(val, values, entry) {
			if (expr_regexec(ex, ml, key->val, val->val,
				    env->ev_options & OPTION_DRYRUN))
				continue;
			return EXPR_MATCH;
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

	TAILQ_FOREACH(str, ex->strings, entry) {
		if (message_has_label(msg, str->val))
			continue;

		/* The label cannot be empty, enforced by the config parser */
		append(&buf, &bufsiz, &buflen, str->val);
		append(&buf, &bufsiz, &buflen, " ");
	}
	if (buflen > 0)
		buf[buflen - 1] = '\0';

	message_set_header(msg, "X-Label", buf);

	matches_append(ml, ex->match);

	return EXPR_MATCH;
}

static int
expr_eval_move(struct expr *ex, struct match_list *ml,
    struct message *UNUSED(msg), const struct environment *UNUSED(env))
{
	const struct string *str;
	size_t len;

	str = TAILQ_FIRST(ex->strings);
	len = sizeof(ml->ml_maildir);
	if (strlcpy(ml->ml_maildir, str->val, len) >= len)
		errc(1, ENAMETOOLONG, "%s", __func__);

	matches_append(ml, ex->match);

	return EXPR_MATCH;
}

static int
expr_eval_neg(struct expr *ex, struct match_list *ml, struct message *msg,
    const struct environment *env)
{
	assert(ex->rhs == NULL);

	switch (expr_eval(ex->lhs, ml, msg, env)) {
	case EXPR_ERROR:
		return EXPR_ERROR;
	case EXPR_NOMATCH:
		return EXPR_MATCH;
	}

	/* No match, invalidate match below expression. */
	matches_clear(ml);
	return EXPR_NOMATCH;
}

static int
expr_eval_new(struct expr *UNUSED(ex), struct match_list *UNUSED(ml),
    struct message *msg, const struct environment *UNUSED(env))
{
	char buf[NAME_MAX];

	if (pathslice(msg->me_path, buf, -2, -2) == NULL || strcmp(buf, "new"))
		return EXPR_NOMATCH;
	return EXPR_MATCH;
}

static int
expr_eval_old(struct expr *UNUSED(ex), struct match_list *UNUSED(ml),
    struct message *msg, const struct environment *UNUSED(env))
{
	char buf[NAME_MAX];

	switch (message_flags_isset(&msg->me_flags, 'S')) {
	case -1:
		return EXPR_ERROR;
	case 1:
		return EXPR_NOMATCH;
	}
	if (pathslice(msg->me_path, buf, -2, -2) == NULL || strcmp(buf, "cur"))
		return EXPR_NOMATCH;
	return EXPR_MATCH;
}

static int
expr_eval_or(struct expr *ex, struct match_list *ml, struct message *msg,
    const struct environment *env)
{
	int e;

	if ((e = expr_eval(ex->lhs, ml, msg, env)) != EXPR_NOMATCH)
		return e; /* match or error, short-circuit */
	return expr_eval(ex->rhs, ml, msg, env);
}

static int
expr_eval_pass(struct expr *ex, struct match_list *ml,
    struct message *UNUSED(msg), const struct environment *UNUSED(env))
{
	matches_append(ml, ex->match);

	/*
	 * Return no match in order to continue evaluation. The return value is
	 * later inverted by expr_eval_block().
	 */
	return EXPR_NOMATCH;
}

static int
expr_eval_reject(struct expr *ex, struct match_list *ml,
    struct message *UNUSED(msg), const struct environment *UNUSED(env))
{
	size_t len;

	matches_append(ml, ex->match);

	len = sizeof(ml->ml_path);
	if (strlcpy(ml->ml_path, "<reject>", len) >= len)
		errc(1, ENAMETOOLONG, "%s", __func__);

	return EXPR_MATCH;
}

static unsigned int
expr_flags(const struct expr *ex)
{
	switch (ex->type) {
	case EXPR_TYPE_BLOCK:
		return 0;
	case EXPR_TYPE_AND:
		return 0;
	case EXPR_TYPE_OR:
		return 0;
	case EXPR_TYPE_NEG:
		return 0;
	case EXPR_TYPE_ALL:
		return 0;
	case EXPR_TYPE_ATTACHMENT:
		return 0;
	case EXPR_TYPE_ATTACHMENT_BODY:
		return EXPR_FLAG_INSPECT | EXPR_FLAG_MATCH;
	case EXPR_TYPE_ATTACHMENT_HEADER:
		return EXPR_FLAG_INSPECT | EXPR_FLAG_MATCH;
	case EXPR_TYPE_BODY:
		return EXPR_FLAG_INSPECT | EXPR_FLAG_MATCH;
	case EXPR_TYPE_DATE:
		return EXPR_FLAG_INSPECT | EXPR_FLAG_MATCH;
	case EXPR_TYPE_HEADER:
		return EXPR_FLAG_INSPECT | EXPR_FLAG_MATCH;
	case EXPR_TYPE_NEW:
		return 0;
	case EXPR_TYPE_OLD:
		return 0;
	case EXPR_TYPE_MOVE:
		return EXPR_FLAG_ACTION | EXPR_FLAG_MATCH;
	case EXPR_TYPE_FLAG:
		return EXPR_FLAG_ACTION | EXPR_FLAG_MATCH;
	case EXPR_TYPE_DISCARD:
		return EXPR_FLAG_ACTION | EXPR_FLAG_MATCH;
	case EXPR_TYPE_BREAK:
		return EXPR_FLAG_ACTION | EXPR_FLAG_MATCH;
	case EXPR_TYPE_LABEL:
		return EXPR_FLAG_ACTION | EXPR_FLAG_MATCH;
	case EXPR_TYPE_PASS:
		return EXPR_FLAG_ACTION | EXPR_FLAG_MATCH;
	case EXPR_TYPE_REJECT:
		return EXPR_FLAG_ACTION | EXPR_FLAG_MATCH;
	}
	return 0;
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
	n = fprintf(fh, "%s:%d: ", path, ex->lno);
	if (n > 0)
		nwrite += n;
	return nwrite;
}

static int
expr_regexec(struct expr *ex, struct match_list *ml, const char *key,
    const char *val, int dryrun)
{
	if (regexec(&ex->ex_re.r_pattern, val, ex->ex_re.r_nmatches,
	    ex->ex_re.r_matches, 0))
		return 1;

	matches_append(ml, ex->match);

	match_copy(ex->match, val, ex->ex_re.r_matches, ex->ex_re.r_nmatches);
	if (dryrun) {
		ex->match->mh_key = strdup(key);
		if (ex->match->mh_key == NULL)
			err(1, NULL);
		ex->match->mh_val = strdup(val);
		if (ex->match->mh_val == NULL)
			err(1, NULL);
	}

	return 0;
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
			newsiz = BUFSIZ;
		*buf = realloc(*buf, newsiz);
		if (*buf == NULL)
			err(1, NULL);
		*bufsiz = newsiz;
	}
	memcpy(*buf + *buflen, str, len);
	*buflen += len;

	return len;
}
