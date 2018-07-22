#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <regex.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

struct rule {
	struct expr *expr;
	int cookie;
};

struct expr {
	enum expr_type type;
	int cookie;

	struct string_list *strings;

	regex_t pattern;
	regmatch_t *matches;
	size_t nmatches;

	struct match *match;

	struct expr *lhs;
	struct expr *rhs;
};

struct match {
	const char *dest;

	/* Everything after this field will be zeroed out by match_reset(). */
	int begzero;

	char **matches;
	size_t nmatches;

	const char *key;
	const char *val;
	size_t valbeg;
	size_t valend;
};

static int expr_eval(struct expr *, const struct message *,
    struct match *match, int);
static int expr_eval_body(struct expr *, const struct message *,
    struct match *);
static int expr_eval_header(struct expr *, const struct message *,
    struct match *);
static int expr_eval_move(struct expr *, const struct message *,
    struct match *);
static int expr_eval_new(struct expr *, const struct message *,
    struct match *);
static void expr_free(struct expr *);
static void expr_inspect(const struct expr *, FILE *, int);
static void expr_inspect_body(const struct expr *, FILE *);
static void expr_inspect_header(const struct expr *, FILE *);

static void match_copy(struct match *, const char *, const regmatch_t *,
    size_t);
static const char *match_get(const struct match *match, unsigned long n);
static const char *match_interpolate(const struct match *,
    const struct message *);
static void match_reset(struct match *match);

struct rule *
rule_alloc(struct expr *ex)
{
	struct rule *rl;

	rl = calloc(1, sizeof(*rl));
	if (rl == NULL)
		err(1, NULL);
	rl->expr = ex;

	return rl;
}

void
rule_free(struct rule *rl)
{
	if (rl == NULL)
		return;

	expr_free(rl->expr);
	free(rl);
}

void
rule_inspect(const struct rule *rl, FILE *fh)
{
	expr_inspect(rl->expr, fh, rl->cookie);
}

const char *
rule_eval(struct rule *rl, const struct message *msg)
{
	struct match match;
	const char *path = NULL;

	memset(&match, 0, sizeof(match));
	rl->cookie++;
	if (expr_eval(rl->expr, msg, &match, rl->cookie))
		goto done;
	path = match_interpolate(&match, msg);

done:
	match_reset(&match);
	return path;
}

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
	case EXPR_TYPE_BODY:
	case EXPR_TYPE_HEADER:
		ex->match = calloc(1, sizeof(*ex->match));
		if (ex->match == NULL)
			err(1, NULL);
		break;
	case EXPR_TYPE_AND:
	case EXPR_TYPE_OR:
	case EXPR_TYPE_NEG:
	case EXPR_TYPE_NEW:
	case EXPR_TYPE_MOVE:
		break;
	}
	return ex;
}

void
expr_set_strings(struct expr *ex, struct string_list *strings)
{
	assert(ex->type == EXPR_TYPE_HEADER || ex->type == EXPR_TYPE_MOVE);
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

static int
expr_eval(struct expr *ex, const struct message *msg, struct match *match,
    int cookie)
{
	int res = 1;

	switch (ex->type) {
	case EXPR_TYPE_AND:
		res = expr_eval(ex->lhs, msg, match, cookie);
		if (res)
			break; /* no match, short-circuit */
		res = expr_eval(ex->rhs, msg, match, cookie);
		break;
	case EXPR_TYPE_OR:
		res = expr_eval(ex->lhs, msg, match, cookie);
		if (res == 0)
			break; /* match, short-circuit */
		res = expr_eval(ex->rhs, msg, match, cookie);
		break;
	case EXPR_TYPE_NEG:
		assert(ex->rhs == NULL);
		res = !expr_eval(ex->lhs, msg, match, cookie);
		/* On non-match, invalidate match below expression. */
		if (res)
			match_reset(match);
		break;
	case EXPR_TYPE_BODY:
		res = expr_eval_body(ex, msg, match);
		break;
	case EXPR_TYPE_HEADER:
		res = expr_eval_header(ex, msg, match);
		break;
	case EXPR_TYPE_NEW:
		res = expr_eval_new(ex, msg, match);
		break;
	case EXPR_TYPE_MOVE:
		res = expr_eval_move(ex, msg, match);
		break;
	}
	if (res == 0) {
		/* Mark expression as visited on match. */
		ex->cookie = cookie;
	}

	return res;
}

static int
expr_eval_body(struct expr *ex, const struct message *msg, struct match *match)
{
	const char *body;

	assert(ex->nmatches > 0);

	body = message_get_body(msg);
	if (body == NULL)
		return 1;
	if (regexec(&ex->pattern, body, ex->nmatches, ex->matches, 0))
		return 1;

	match_reset(ex->match);
	ex->match->key = NULL;
	ex->match->val = body;
	ex->match->valbeg = ex->matches[0].rm_so;
	ex->match->valend = ex->matches[0].rm_eo;
	match_copy(match, body, ex->matches, ex->nmatches);
	return 0;
}

static int
expr_eval_header(struct expr *ex, const struct message *msg,
    struct match *match)
{
	const char *val;
	const struct string *str;

	assert(ex->strings != NULL);
	assert(ex->nmatches > 0);

	TAILQ_FOREACH(str, ex->strings, entry) {
		val = message_get_header(msg, str->val);
		if (val == NULL)
			continue;
		if (regexec(&ex->pattern, val, ex->nmatches, ex->matches, 0))
			continue;

		match_reset(ex->match);
		ex->match->key = str->val;
		ex->match->val = val;
		ex->match->valbeg = ex->matches[0].rm_so;
		ex->match->valend = ex->matches[0].rm_eo;
		match_copy(match, val, ex->matches, ex->nmatches);
		return 0;
	}
	return 1;
}

static int
expr_eval_move(struct expr *ex,
    const struct message *msg __attribute__((__unused__)), struct match *match)
{
	struct string *str;

	str = TAILQ_FIRST(ex->strings);
	match->dest = str->val;
	return 0;
}

static int
expr_eval_new(struct expr *ex __attribute__((__unused__)),
    const struct message *msg, struct match *match __attribute__((__unused__)))
{
	const char *dirname;

	dirname = message_get_dirname(msg);
	if (dirname == NULL || strcmp(dirname, "new"))
		return 1;
	return 0;
}

static void
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

static void
expr_inspect(const struct expr *ex, FILE *fh, int cookie)
{
	/* Ensure expression was visited during last call to expr_eval() */
	if (ex->cookie != cookie)
		return;

	switch (ex->type) {
	case EXPR_TYPE_AND:
	case EXPR_TYPE_OR:
		expr_inspect(ex->lhs, fh, cookie);
		expr_inspect(ex->rhs, fh, cookie);
		break;
	case EXPR_TYPE_BODY:
		expr_inspect_body(ex, fh);
		break;
	case EXPR_TYPE_HEADER:
		expr_inspect_header(ex, fh);
		break;
	case EXPR_TYPE_NEG:
	case EXPR_TYPE_NEW:
	case EXPR_TYPE_MOVE:
		break;
	}
}

static void
expr_inspect_body(const struct expr *ex, FILE *fh)
{
	const struct match *match;
	const char *beg, *end, *p;
	int len, padbeg, padend;

	match = ex->match;
	beg = match->val;
	for (;;) {
		if ((p = strchr(beg, '\n')) == NULL ||
		    p > match->val + match->valbeg)
			break;
		beg = p + 1;
	}
	if ((end = strchr(beg, '\n')) == NULL)
		end = match->val + match->valend;
	/* Do not handle a match spanning over multiple lines for now. */
	if (match->valend > (size_t)(end - match->val))
		return;
	padbeg = match->valbeg - (beg - match->val);
	padend = match->valend - match->valbeg;
	if (padend >= 2)
		padend -= 2;
	len = end - beg;
	fprintf(fh, "%.*s\n%*s^%*s$\n", len, beg, padbeg, "", padend, "");
}

void
expr_inspect_header(const struct expr *ex, FILE *fh)
{
	const struct match *match;
	int lenval, padbeg, padend;

	match = ex->match;
	lenval = strlen(match->val);
	padbeg = strlen(match->key) + 2 + match->valbeg;
	padend = match->valend - match->valbeg;
	if (padend >= 2)
		padend -= 2;
	fprintf(fh, "%s: %.*s\n%*s^%*s$\n",
	    match->key, lenval, match->val, padbeg, "", padend, "");
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

static const char *
match_interpolate(const struct match *match, const struct message *msg)
{
	static char buf[PATH_MAX];
	char path[PATH_MAX];
	const char *dirname, *sub;
	char *end;
	unsigned long bf;
	size_t i = 0;
	size_t j = 0;

	assert(match != NULL);

	dirname = message_get_dirname(msg);
	if (dirname == NULL) {
		warnx("%s: dirname not found", message_get_path(msg));
		return NULL;
	}
	pathjoin(path, match->dest, dirname, NULL);

	while (path[i] != '\0') {
		if (path[i] == '\\' && isdigit(path[i + 1])) {
			errno = 0;
			bf = strtoul(path + i + 1, &end, 10);
			if ((errno == ERANGE && bf == ULONG_MAX) ||
			    ((sub = match_get(match, bf)) == NULL)) {
				warnx("%s: invalid back-reference in "
				    "destination", path);
				return NULL;
			}
			for (; *sub != '\0'; sub++) {
				if (j == sizeof(buf) - 1)
					goto toolong;
				buf[j++] = *sub;
			}
			i = end - path;
			continue;
		}
		if (j == sizeof(buf) - 1)
			goto toolong;
		buf[j++] = path[i++];
	}
	assert(j < sizeof(buf));
	buf[j] = '\0';
	return buf;

toolong:
	warnx("%s: destination too long", path);
	return NULL;
}

static void
match_reset(struct match *match)
{
	size_t i, len;

	if (match == NULL)
		return;

	for (i = 0; i < match->nmatches; i++)
		free(match->matches[i]);
	free(match->matches);

	/* Reset everything after the begzero field inclusively. */
	len = sizeof(*match) - ((ptrdiff_t)&match->begzero - (ptrdiff_t)match);
	memset(&match->begzero, 0, len);
}
