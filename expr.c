#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

#define EXPR_EVAL_ARGS	struct expr *, struct expr *, const struct message *, \
	const struct environment *

static int expr_eval1(EXPR_EVAL_ARGS);
static int expr_eval_all(EXPR_EVAL_ARGS);
static int expr_eval_and(EXPR_EVAL_ARGS);
static int expr_eval_block(EXPR_EVAL_ARGS);
static int expr_eval_body(EXPR_EVAL_ARGS);
static int expr_eval_date(EXPR_EVAL_ARGS);
static int expr_eval_discard(EXPR_EVAL_ARGS);
static int expr_eval_flag(EXPR_EVAL_ARGS);
static int expr_eval_header(EXPR_EVAL_ARGS);
static int expr_eval_move(EXPR_EVAL_ARGS);
static int expr_eval_neg(EXPR_EVAL_ARGS);
static int expr_eval_new(EXPR_EVAL_ARGS);
static int expr_eval_old(EXPR_EVAL_ARGS);
static int expr_eval_or(EXPR_EVAL_ARGS);
static int expr_eval_pass(EXPR_EVAL_ARGS);

static void expr_inspect1(const struct expr *, const struct expr *, FILE *);
static void expr_inspect_body(const struct expr *, FILE *);
static void expr_inspect_date(const struct expr *, FILE *);
static void expr_inspect_header(const struct expr *, FILE *);

static int expr_regexec(struct expr *, struct match *, const char *,
    const char *);

static void match_copy(struct match *, const char *, const regmatch_t *,
    size_t);
static const char *match_get(const struct match *, unsigned long n);
static int match_interpolate(struct match *);
static void match_reset(struct match *);

struct expr *
expr_alloc(enum expr_type type, struct expr *lhs, struct expr *rhs)
{
	struct expr *ex;

	ex = calloc(1, sizeof(*ex));
	if (ex == NULL)
		err(1, NULL);
	ex->type = type;
	ex->lhs = lhs;
	ex->rhs = rhs;
	switch (ex->type) {
	case EXPR_TYPE_BLOCK:
	case EXPR_TYPE_BODY:
	case EXPR_TYPE_DATE:
	case EXPR_TYPE_HEADER:
		ex->match = calloc(1, sizeof(*ex->match));
		if (ex->match == NULL)
			err(1, NULL);
		break;
	case EXPR_TYPE_AND:
	case EXPR_TYPE_OR:
	case EXPR_TYPE_NEG:
	case EXPR_TYPE_ALL:
	case EXPR_TYPE_NEW:
	case EXPR_TYPE_OLD:
	case EXPR_TYPE_MOVE:
	case EXPR_TYPE_FLAG:
	case EXPR_TYPE_DISCARD:
	case EXPR_TYPE_PASS:
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
	regfree(&ex->pattern);
	free(ex->match);
	free(ex->matches);
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
	int rflags = REG_EXTENDED;

	assert(ex->type == EXPR_TYPE_BODY || ex->type == EXPR_TYPE_HEADER);
	assert(ex->nmatches == 0);

	if (flags & EXPR_PATTERN_ICASE) {
		rflags |= REG_ICASE;
		flags &= ~EXPR_PATTERN_ICASE;
	}
	assert(flags == 0);

	if ((ret = regcomp(&ex->pattern, pattern, rflags)) != 0) {
		if (errstr != NULL) {
			regerror(ret, &ex->pattern, buf, sizeof(buf));
			*errstr = buf;
		}
		return 1;
	}
	ex->nmatches = ex->pattern.re_nsub + 1;
	ex->matches = reallocarray(NULL, ex->nmatches, sizeof(*ex->matches));
	if (ex->matches == NULL)
		err(1, NULL);

	return 0;
}

const struct match *
expr_eval(struct expr *ex, const struct message *msg,
    const struct environment *env)
{
	match_reset(ex->match);
	ex->cookie++;
	if (expr_eval1(ex, ex, msg, env))
		return NULL;
	switch (ex->match->action->type) {
	case EXPR_TYPE_FLAG:
	case EXPR_TYPE_MOVE:
		if (match_interpolate(ex->match))
			return NULL;
		break;
	default:
		break;
	}
	return ex->match;
}

int
expr_count(const struct expr *ex, enum expr_type type)
{
	int acc = 0;

	if (ex == NULL)
		return 0;

	if (ex->type == type)
		acc = 1;
	return acc + expr_count(ex->lhs, type) + expr_count(ex->rhs, type);
}

int
expr_count_actions(const struct expr *ex)
{
	int acc = 0;

	if (ex == NULL)
		return 0;

	switch (ex->type) {
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
	case EXPR_TYPE_PASS:
		acc = 1;
		break;
	}
	return acc + expr_count_actions(ex->lhs) + expr_count_actions(ex->rhs);
}

void
expr_inspect(const struct expr *ex, FILE *fh)
{
	expr_inspect1(ex, ex, fh);
}

static int
expr_eval1(struct expr *root, struct expr *ex, const struct message *msg,
    const struct environment *env)
{
	int res = 1;

	switch (ex->type) {
	case EXPR_TYPE_BLOCK:
		res = expr_eval_block(root, ex, msg, env);
		break;
	case EXPR_TYPE_AND:
		res = expr_eval_and(root, ex, msg, env);
		break;
	case EXPR_TYPE_OR:
		res = expr_eval_or(root, ex, msg, env);
		break;
	case EXPR_TYPE_NEG:
		res = expr_eval_neg(root, ex, msg, env);
		break;
	case EXPR_TYPE_ALL:
		res = expr_eval_all(root, ex, msg, env);
		break;
	case EXPR_TYPE_BODY:
		res = expr_eval_body(root, ex, msg, env);
		break;
	case EXPR_TYPE_DATE:
		res = expr_eval_date(root, ex, msg, env);
		break;
	case EXPR_TYPE_HEADER:
		res = expr_eval_header(root, ex, msg, env);
		break;
	case EXPR_TYPE_NEW:
		res = expr_eval_new(root, ex, msg, env);
		break;
	case EXPR_TYPE_OLD:
		res = expr_eval_old(root, ex, msg, env);
		break;
	case EXPR_TYPE_MOVE:
		res = expr_eval_move(root, ex, msg, env);
		break;
	case EXPR_TYPE_FLAG:
		res = expr_eval_flag(root, ex, msg, env);
		break;
	case EXPR_TYPE_DISCARD:
		res = expr_eval_discard(root, ex, msg, env);
		break;
	case EXPR_TYPE_PASS:
		res = expr_eval_pass(root, ex, msg, env);
		break;
	}
	if (res == 0) {
		/* Mark expression as visited on match. */
		ex->cookie = root->cookie;
	}

	return res;
}

static int
expr_eval_all(struct expr *UNUSED(root), struct expr *UNUSED(ex),
    const struct message *UNUSED(msg), const struct environment *UNUSED(env))
{
	return 0;
}

static int
expr_eval_and(struct expr *root, struct expr *ex, const struct message *msg,
    const struct environment *env)
{
	if (expr_eval1(root, ex->lhs, msg, env))
		return 1; /* no match, short-circuit */
	return expr_eval1(root, ex->rhs, msg, env);
}

static int
expr_eval_block(struct expr *root, struct expr *ex, const struct message *msg,
    const struct environment *env)
{
	const struct expr *action;
	int res;

	res = expr_eval1(root, ex->lhs, msg, env);
	action = root->match->action;
	if (action && action->type == EXPR_TYPE_PASS) {
		root->match->action = NULL;
		return 1; /* pass, continue evaluation */
	}
	return res;
}

static int
expr_eval_body(struct expr *root, struct expr *ex, const struct message *msg,
    const struct environment *UNUSED(env))
{
	assert(ex->nmatches > 0);

	if (msg->body == NULL)
		return 1;
	if (expr_regexec(ex, root->match, NULL, msg->body))
		return 1;
	return 0;
}

static int
expr_eval_date(struct expr *UNUSED(root), struct expr *ex,
    const struct message *msg, const struct environment *env)
{
	const struct string_list *dates;
	const struct string *str;
	time_t delta, tim;

	dates = message_get_header(msg, "Date");
	if (dates == NULL)
		return 1;
	str = TAILQ_FIRST(dates);
	if (time_parse(str->val, &tim, env))
		return 1;

	match_reset(ex->match);
	ex->match->key = "Date";
	ex->match->val = str->val;

	delta = env->now - tim;
	switch (ex->date.cmp) {
	case EXPR_CMP_LT:
		if (delta < ex->date.age)
			return 0;
		break;
	case EXPR_CMP_GT:
		if (delta > ex->date.age)
			return 0;
		break;
	}
	return 1;
}

static int
expr_eval_discard(struct expr *root, struct expr *ex,
    const struct message *UNUSED(msg), const struct environment *UNUSED(env))
{
	size_t len;

	root->match->action = ex;

	/* Populate the path in case of a dry run. */
	len = sizeof(root->match->path);
	if (strlcpy(root->match->path, "<discard>", len) >= len)
		errc(1, ENAMETOOLONG, "%s", __func__);

	return 0;
}

static int
expr_eval_flag(struct expr *root, struct expr *ex, const struct message *msg,
    const struct environment *UNUSED(env))
{
	struct string *str;
	size_t len;

	root->match->action = ex;

	str = TAILQ_FIRST(ex->strings);
	len = sizeof(root->match->subdir);
	if (strlcpy(root->match->subdir, str->val, len) >= len)
		errc(1, ENAMETOOLONG, "%s", __func__);

	/* A move action might be missing. */
	if (strlen(root->match->maildir) == 0) {
		if (pathslice(msg->path, root->match->maildir, 0, -2) == NULL)
			errx(1, "%s: %s: maildir not found", __func__,
			    msg->path);
	}

	return 0;
}

static int
expr_eval_header(struct expr *root, struct expr *ex, const struct message *msg,
    const struct environment *UNUSED(env))
{
	const struct string_list *values;
	const struct string *key, *val;

	assert(ex->strings != NULL);
	assert(ex->nmatches > 0);

	TAILQ_FOREACH(key, ex->strings, entry) {
		values = message_get_header(msg, key->val);
		if (values == NULL)
			continue;

		TAILQ_FOREACH(val, values, entry) {
                        if (expr_regexec(ex, root->match, key->val, val->val))
				continue;
			return 0;
		}
	}
	return 1;
}

static int
expr_eval_move(struct expr *root, struct expr *ex, const struct message *msg,
    const struct environment *UNUSED(env))
{
	struct string *str;
	size_t len;

	root->match->action = ex;

	str = TAILQ_FIRST(ex->strings);
	len = sizeof(root->match->maildir);
	if (strlcpy(root->match->maildir, str->val, len) >= len)
		errc(1, ENAMETOOLONG, "%s", __func__);

	/* A flag action might already have been evaluted. */
	if (strlen(root->match->subdir) == 0) {
		if (pathslice(msg->path, root->match->subdir, -2, -2) == NULL)
			errx(1, "%s: %s: subdir not found",
			    __func__, msg->path);
	}

	return 0;
}

static int
expr_eval_neg(struct expr *root, struct expr *ex, const struct message *msg,
    const struct environment *env)
{
	assert(ex->rhs == NULL);

	if (expr_eval1(root, ex->lhs, msg, env))
		return 0;

	/* Non-match, invalidate match below expression. */
	match_reset(root->match);
	return 1;
}

static int
expr_eval_new(struct expr *UNUSED(root), struct expr *UNUSED(ex),
    const struct message *msg, const struct environment *UNUSED(env))
{
	char buf[NAME_MAX];

	if (pathslice(msg->path, buf, -2, -2) == NULL || strcmp(buf, "new"))
		return 1;
	return 0;
}

static int
expr_eval_old(struct expr *UNUSED(root), struct expr *UNUSED(ex),
    const struct message *msg, const struct environment *UNUSED(env))
{
	char buf[NAME_MAX];

	if (message_has_flags(msg, 'S'))
		return 1;
	if (pathslice(msg->path, buf, -2, -2) == NULL || strcmp(buf, "cur"))
		return 1;
	return 0;
}

static int
expr_eval_or(struct expr *root, struct expr *ex, const struct message *msg,
    const struct environment *env)
{
	if (expr_eval1(root, ex->lhs, msg, env) == 0)
		return 0; /* match, short-circuit */
	return expr_eval1(root, ex->rhs, msg, env);
}

static int
expr_eval_pass(struct expr *root, struct expr *ex,
    const struct message *UNUSED(msg), const struct environment *UNUSED(env))
{
	root->match->action = ex;
	return 0;
}

static void
expr_inspect1(const struct expr *root, const struct expr *ex, FILE *fh)
{
	/* Ensure expression was visited during last call to expr_eval(). */
	if (root->cookie != ex->cookie)
		return;

	switch (ex->type) {
	case EXPR_TYPE_BLOCK:
		expr_inspect1(root, ex->lhs, fh);
		break;
	case EXPR_TYPE_AND:
	case EXPR_TYPE_OR:
		expr_inspect1(root, ex->lhs, fh);
		expr_inspect1(root, ex->rhs, fh);
		break;
	case EXPR_TYPE_BODY:
		expr_inspect_body(ex, fh);
		break;
	case EXPR_TYPE_DATE:
		expr_inspect_date(ex, fh);
		break;
	case EXPR_TYPE_HEADER:
		expr_inspect_header(ex, fh);
		break;
	case EXPR_TYPE_NEG:
	case EXPR_TYPE_ALL:
	case EXPR_TYPE_NEW:
	case EXPR_TYPE_OLD:
	case EXPR_TYPE_MOVE:
	case EXPR_TYPE_FLAG:
	case EXPR_TYPE_DISCARD:
	case EXPR_TYPE_PASS:
		break;
	}
}

static void
expr_inspect_body(const struct expr *ex, FILE *fh)
{
	const struct match *match;
	const char *lbeg, *lend, *p;
	unsigned int i;
	int beg, end, indent, len;

	match = ex->match;

	for (i = 0; i < ex->nmatches; i++) {
		beg = ex->matches[i].rm_so;
		end = ex->matches[i].rm_eo;

		lbeg = match->val;
		for (;;) {
			if ((p = strchr(lbeg, '\n')) == NULL ||
			    p > match->val + beg)
				break;
			lbeg = p + 1;
		}
		lend = strchr(lbeg, '\n');
		if (lend == NULL)
			lend = match->val + end;
		/* Skip matches spanning over multiple lines. */
		if (match->val + end > lend)
			continue;

		indent = beg - (lbeg - match->val);
		len = end - beg;
		if (len >= 2)
			len -= 2;
		fprintf(fh, "%.*s\n%*s^%*s$\n",
		    (int)(lend - lbeg), lbeg, indent, "", len, "");
	}
}

static void
expr_inspect_date(const struct expr *ex, FILE *fh)
{
	const struct match *match;
	int end, indent;

	match = ex->match;
	indent = strlen(match->key) + 2;
	end = strlen(match->val);
	if (end >= 2)
		end -= 2;
	fprintf(fh, "%s: %s\n", match->key, match->val);
	fprintf(fh, "%*s^%*s$\n", indent, "", end, "");
}

static void
expr_inspect_header(const struct expr *ex, FILE *fh)
{
	const struct match *match;
	unsigned int i;
	int beg, end, indent, len;

	match = ex->match;
	indent = strlen(match->key) + 2;

	fprintf(fh, "%s: ", match->key);
	for (i = 0; i < ex->nmatches; i++) {
		if (i > 0)
			fprintf(fh, "%*s", indent, "");
		fprintf(fh, "%s\n", match->val);

		beg = ex->matches[i].rm_so;
		end = ex->matches[i].rm_eo;
		len = end - beg;
		if (len >= 2)
			len -= 2;
		fprintf(fh, "%*s^%*s$\n", indent + beg, "", len, "");
	}
}

static int
expr_regexec(struct expr *ex, struct match *match, const char *key,
    const char *val)
{
	if (regexec(&ex->pattern, val, ex->nmatches, ex->matches, 0))
		return 1;

	match_reset(ex->match);
	ex->match->key = key;
	ex->match->val = val;
	match_copy(match, val, ex->matches, ex->nmatches);
	return 0;
}

static void
match_copy(struct match *match, const char *str, const regmatch_t *off,
    size_t nmemb)
{
	char *cpy;
	size_t i, len;

	if (match->nmatches > 0)
		return;

	match->matches = reallocarray(NULL, nmemb, sizeof(*match->matches));
	if (match->matches == NULL)
		err(1, NULL);
	match->nmatches = nmemb;
	for (i = 0; i < nmemb; i++) {
		len = off[i].rm_eo - off[i].rm_so;
		cpy = strndup(str + off[i].rm_so, len);
		if (cpy == NULL)
			err(1, NULL);
		match->matches[i] = cpy;
	}
}

static const char *
match_get(const struct match *match, unsigned long n)
{
	if (n >= match->nmatches)
		return NULL;
	return match->matches[n];
}

static int
match_interpolate(struct match *match)
{
	char path[PATH_MAX];
	const char *sub;
	char *end;
	unsigned long bf;
	size_t i = 0;
	size_t j = 0;

	assert(match != NULL);

	pathjoin(path, match->maildir, match->subdir, NULL);

	while (path[i] != '\0') {
		if (path[i] == '\\' && isdigit(path[i + 1])) {
			errno = 0;
			bf = strtoul(path + i + 1, &end, 10);
			if ((errno == ERANGE && bf == ULONG_MAX) ||
			    ((sub = match_get(match, bf)) == NULL)) {
				warnx("%s: invalid back-reference in "
				    "destination", path);
				return 1;
			}
			for (; *sub != '\0'; sub++) {
				if (j == sizeof(match->path) - 1)
					goto toolong;
				match->path[j++] = *sub;
			}
			i = end - path;
			continue;
		}
		if (j == sizeof(match->path) - 1)
			goto toolong;
		match->path[j++] = path[i++];
	}
	assert(j < sizeof(match->path));
	match->path[j] = '\0';
	return 0;

toolong:
	warnx("%s: destination too long", path);
	return 1;
}

static void
match_reset(struct match *match)
{
	unsigned int i;

	if (match == NULL)
		return;

	for (i = 0; i < match->nmatches; i++)
		free(match->matches[i]);
	free(match->matches);

	memset(match, 0, sizeof(*match));
}
