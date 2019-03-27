#include "config.h"

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

static void expr_inspect_date(const struct expr *, FILE *,
    const struct environment *);
static void expr_inspect_header(const struct expr *, FILE *,
    const struct environment *);
static int expr_inspect_prefix(const struct expr *, FILE *,
    const struct environment *);

static int expr_regexec(struct expr *, struct match_list *, const char *,
    const char *, int);

static size_t append(char **, size_t *, size_t *, const char *);

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
	switch (ex->type) {
	case EXPR_TYPE_ATTACHMENT:
	case EXPR_TYPE_BLOCK:
	case EXPR_TYPE_BODY:
	case EXPR_TYPE_DATE:
	case EXPR_TYPE_HEADER:
	case EXPR_TYPE_MOVE:
	case EXPR_TYPE_FLAG:
	case EXPR_TYPE_DISCARD:
	case EXPR_TYPE_BREAK:
	case EXPR_TYPE_LABEL:
		ex->match = calloc(1, sizeof(*ex->match));
		if (ex->match == NULL)
			err(1, NULL);
		ex->match->mh_expr = ex;
		break;
	case EXPR_TYPE_AND:
	case EXPR_TYPE_OR:
	case EXPR_TYPE_NEG:
	case EXPR_TYPE_ALL:
	case EXPR_TYPE_NEW:
	case EXPR_TYPE_OLD:
		break;
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
}

void
expr_set_strings(struct expr *ex, struct string_list *strings)
{
	ex->strings = strings;
}

int
expr_set_pattern(struct expr *ex, const char *pattern, int flags,
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
	}

	return 1;
}

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

int
expr_count_actions(const struct expr *ex)
{
	int n = 0;

	if (ex == NULL)
		return 0;

	switch (ex->type) {
	case EXPR_TYPE_ATTACHMENT:
	case EXPR_TYPE_BLOCK:
	case EXPR_TYPE_AND:
	case EXPR_TYPE_OR:
	case EXPR_TYPE_NEG:
	case EXPR_TYPE_ALL:
	case EXPR_TYPE_BODY:
	case EXPR_TYPE_DATE:
	case EXPR_TYPE_HEADER:
	case EXPR_TYPE_NEW:
	case EXPR_TYPE_OLD:
		break;
	case EXPR_TYPE_MOVE:
	case EXPR_TYPE_FLAG:
	case EXPR_TYPE_DISCARD:
	case EXPR_TYPE_BREAK:
	case EXPR_TYPE_LABEL:
		n = 1;
		break;
	}
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

void
expr_inspect(const struct expr *ex, FILE *fh, const struct environment *env)
{
	switch (ex->type) {
	case EXPR_TYPE_DATE:
		expr_inspect_date(ex, fh, env);
		break;
	case EXPR_TYPE_ATTACHMENT:
	case EXPR_TYPE_BODY:
	case EXPR_TYPE_HEADER:
		expr_inspect_header(ex, fh, env);
		break;
	case EXPR_TYPE_BLOCK:
	case EXPR_TYPE_AND:
	case EXPR_TYPE_OR:
	case EXPR_TYPE_NEG:
	case EXPR_TYPE_ALL:
	case EXPR_TYPE_NEW:
	case EXPR_TYPE_OLD:
	case EXPR_TYPE_MOVE:
	case EXPR_TYPE_FLAG:
	case EXPR_TYPE_DISCARD:
	case EXPR_TYPE_BREAK:
	case EXPR_TYPE_LABEL:
		break;
	}
}

static int
expr_eval_all(struct expr *UNUSED(ex), struct match_list *UNUSED(ml),
    struct message *UNUSED(msg), const struct environment *UNUSED(env))
{
	return 0;
}

static int
expr_eval_and(struct expr *ex, struct match_list *ml, struct message *msg,
    const struct environment *env)
{
	if (expr_eval(ex->lhs, ml, msg, env))
		return 1; /* no match, short-circuit */
	return expr_eval(ex->rhs, ml, msg, env);
}

static int
expr_eval_attachment(struct expr *ex, struct match_list *ml,
    struct message *msg, const struct environment *env)
{
	struct message_list *attachments;
	struct message *attach;
	const char *type;

	attachments = message_get_attachments(msg);
	if (attachments == NULL)
		return 1;

	TAILQ_FOREACH(attach, attachments, entry) {
		type = message_get_header1(attach, "Content-Type");
		if (type == NULL)
			continue;
		log_debug("%s: %s\n", __func__, type);

		if (expr_regexec(ex, ml, "Content-Type", type,
			    env->options & OPTION_DRYRUN))
			continue;

		message_list_free(attachments);
		return 0;
	}

	message_list_free(attachments);
	return 1;
}

static int
expr_eval_block(struct expr *ex, struct match_list *ml,
    struct message *msg, const struct environment *env)
{
	int res;

	res = expr_eval(ex->lhs, ml, msg, env);
	if (matches_find(ml, EXPR_TYPE_BREAK) != NULL) {
		matches_clear(ml);
		return 1; /* break, continue evaluation */
	}
	return res;
}

static int
expr_eval_body(struct expr *ex, struct match_list *ml,
    struct message *msg, const struct environment *env)
{
	assert(ex->ex_re.r_nmatches > 0);

	if (msg->body == NULL)
		return 1;
	if (expr_regexec(ex, ml, "Body", msg->body,
		    env->options & OPTION_DRYRUN))
		return 1;
	return 0;
}

static int
expr_eval_break(struct expr *ex, struct match_list *ml,
    struct message *UNUSED(msg), const struct environment *UNUSED(env))
{
	matches_append(ml, ex->match);
	return 0;
}

static int
expr_eval_date(struct expr *ex, struct match_list *ml,
    struct message *msg, const struct environment *env)
{
	const char *date;
	time_t delta, tim;

	date = message_get_header1(msg, "Date");
	if (date == NULL)
		return 1;
	if (time_parse(date, &tim, env))
		return 1;

	delta = env->now - tim;
	switch (ex->date.cmp) {
	case EXPR_CMP_LT:
		if (!(delta < ex->date.age))
			return 1;
		break;
	case EXPR_CMP_GT:
		if (!(delta > ex->date.age))
			return 1;
		break;
	}

	if (env->options & OPTION_DRYRUN) {
		match_reset(ex->match);

		ex->match->mh_key = strdup("Date");
		if (ex->match->mh_key == NULL)
			err(1, NULL);
		ex->match->mh_val = strdup(date);
		if (ex->match->mh_val == NULL)
			err(1, NULL);
	}

	matches_append(ml, ex->match);

	return 0;
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

	return 0;
}

static int
expr_eval_flag(struct expr *ex, struct match_list *ml,
    struct message *UNUSED(msg), const struct environment *UNUSED(env))
{
	const struct string *str;
	size_t len;

	str = TAILQ_FIRST(ex->strings);
	len = sizeof(ml->ml_maildir);
	if (strlcpy(ml->ml_subdir, str->val, len) >= len)
		errc(1, ENAMETOOLONG, "%s", __func__);

	matches_append(ml, ex->match);

	return 0;
}

static int
expr_eval_header(struct expr *ex, struct match_list *ml,
    struct message *msg, const struct environment *env)
{
	const struct string_list *values;
	const struct string *key, *val;

	assert(ex->strings != NULL);
	assert(ex->ex_re.r_nmatches > 0);

	TAILQ_FOREACH(key, ex->strings, entry) {
		values = message_get_header(msg, key->val);
		if (values == NULL)
			continue;

		TAILQ_FOREACH(val, values, entry) {
			if (expr_regexec(ex, ml, key->val, val->val,
				    env->options & OPTION_DRYRUN))
				continue;
			return 0;
		}
	}
	return 1;
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

		append(&buf, &bufsiz, &buflen, str->val);
		append(&buf, &bufsiz, &buflen, " ");
	}
	if (buflen > 0)
		buf[buflen - 1] = '\0';

	message_set_header(msg, "X-Label", buf);

	matches_append(ml, ex->match);

	return 0;
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

	return 0;
}

static int
expr_eval_neg(struct expr *ex, struct match_list *ml, struct message *msg,
    const struct environment *env)
{
	assert(ex->rhs == NULL);

	if (expr_eval(ex->lhs, ml, msg, env))
		return 0;

	/* Non-match, invalidate match below expression. */
	matches_clear(ml);
	return 1;
}

static int
expr_eval_new(struct expr *UNUSED(ex), struct match_list *UNUSED(ml),
    struct message *msg, const struct environment *UNUSED(env))
{
	char buf[NAME_MAX];

	if (pathslice(msg->path, buf, -2, -2) == NULL || strcmp(buf, "new"))
		return 1;
	return 0;
}

static int
expr_eval_old(struct expr *UNUSED(ex), struct match_list *UNUSED(ml),
    struct message *msg, const struct environment *UNUSED(env))
{
	char buf[NAME_MAX];

	if (message_has_flags(msg, 'S'))
		return 1;
	if (pathslice(msg->path, buf, -2, -2) == NULL || strcmp(buf, "cur"))
		return 1;
	return 0;
}

static int
expr_eval_or(struct expr *ex, struct match_list *ml, struct message *msg,
    const struct environment *env)
{
	if (expr_eval(ex->lhs, ml, msg, env) == 0)
		return 0; /* match, short-circuit */
	return expr_eval(ex->rhs, ml, msg, env);
}

static void
expr_inspect_date(const struct expr *ex, FILE *fh,
    const struct environment *env)
{
	const struct match *match;
	int end, indent;

	match = ex->match;
	indent = strlen(match->mh_key) + 2;
	end = strlen(match->mh_val);
	if (end >= 2)
		end -= 2;
	indent += expr_inspect_prefix(ex, fh, env);
	fprintf(fh, "%s: %s\n", match->mh_key, match->mh_val);
	fprintf(fh, "%*s^%*s$\n", indent, "", end, "");
}

static void
expr_inspect_header(const struct expr *ex, FILE *fh,
    const struct environment *env)
{
	const struct match *match;
	const char *lbeg, *lend, *p;
	unsigned int i;
	int beg, end, indent, len, lindent, plen;
	int printkey = 1;

	match = ex->match;
	indent = strlen(match->mh_key) + 2;

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
		lend = strchr(lbeg, '\n');
		if (lend == NULL)
			lend = match->mh_val + strlen(match->mh_val);

		lindent = beg - (lbeg - match->mh_val) + indent;
		len = end - beg;
		if (len >= 2)
			len -= 2;

		if (printkey) {
			plen = expr_inspect_prefix(ex, fh, env);
			printkey = 0;
			fprintf(fh, "%s: ", match->mh_key);
		} else {
			fprintf(fh, "%*s", indent + plen, "");
		}
		lindent += plen;
		fprintf(fh, "%.*s\n%*s^%*s$\n",
		    (int)(lend - lbeg), lbeg, lindent, "", len, "");
	}
}

static int
expr_inspect_prefix(const struct expr *ex, FILE *fh,
    const struct environment *env)
{
	const char *path;
	size_t len;
	int n;
	int nwrite = 0;

	path = env->confpath;
	len = strlen(env->home);
	if (strncmp(path, env->home, len) == 0) {
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
