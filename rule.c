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

	struct expr_headers *headers;

	regex_t pattern;
	regmatch_t *matches;
	size_t nmatches;

	char *dest;

	struct match *match;

	struct expr *lhs;
	struct expr *rhs;
};

struct header {
	char *key;

	TAILQ_ENTRY(header) entry;
};

TAILQ_HEAD(expr_headers, header);

struct match {
	const char *str;

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

static void match_copy(struct match *, const char *, regmatch_t *, size_t);
static const char *match_get(const struct match *match, unsigned long n);
static const char *match_interpolate(const struct match *);
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
	path = match_interpolate(&match);

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
	case EXPR_TYPE_MOVE:
	case EXPR_TYPE_NEW:
		break;
	}
	return ex;
}

void
expr_set_dest(struct expr *ex, char *dest)
{
	assert(ex->type == EXPR_TYPE_MOVE);
	ex->dest = dest;
}

void
expr_set_headers(struct expr *ex, struct expr_headers *headers)
{
	assert(ex->type == EXPR_TYPE_HEADER);
	ex->headers = headers;
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

struct expr_headers *
expr_headers_alloc(void)
{
	struct expr_headers *headers;

	headers = malloc(sizeof(*headers));
	if (headers == NULL)
		err(1, NULL);
	TAILQ_INIT(headers);
	return headers;
}

void
expr_headers_append(struct expr_headers *headers, char *key)
{
	struct header *hdr;

	hdr = malloc(sizeof(*hdr));
	if (hdr == NULL)
		err(1, NULL);
	hdr->key = key;
	TAILQ_INSERT_TAIL(headers, hdr, entry);
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
	case EXPR_TYPE_MOVE:
		res = expr_eval_move(ex, msg, match);
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
	const struct header *hdr;

	assert(ex->headers != NULL);
	assert(ex->nmatches > 0);

	TAILQ_FOREACH(hdr, ex->headers, entry) {
		val = message_get_header(msg, hdr->key);
		if (val == NULL)
			continue;
		if (regexec(&ex->pattern, val, ex->nmatches, ex->matches, 0))
			continue;

		match_reset(ex->match);
		ex->match->key = hdr->key;
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
	match->str = ex->dest;
	return 0;
}

static int
expr_eval_new(struct expr *ex __attribute__((__unused__)),
    const struct message *msg, struct match *match __attribute__((__unused__)))
{
	const char *beg, *end, *p, *path;

	path = message_get_path(msg);
	beg = end = path;
	for (;;) {
		if ((p = strchr(end, '/')) == NULL)
			break;
		beg = end;
		end = p + 1;
	}
	if (strncmp(beg, "new/", 4))
		return 1;
	return 0;
}

static void
expr_free(struct expr *ex)
{
	struct header *hdr;

	if (ex == NULL)
		return;

	expr_free(ex->lhs);
	expr_free(ex->rhs);
	if (ex->headers != NULL) {
		while ((hdr = TAILQ_FIRST(ex->headers)) != NULL) {
			TAILQ_REMOVE(ex->headers, hdr, entry);
			free(hdr->key);
			free(hdr);
		}
		free(ex->headers);
	}
	match_reset(ex->match);
	regfree(&ex->pattern);
	free(ex->match);
	free(ex->matches);
	free(ex->dest);
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
	case EXPR_TYPE_MOVE:
	case EXPR_TYPE_NEG:
	case EXPR_TYPE_NEW:
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
match_copy(struct match *match, const char *str, regmatch_t *off, size_t nmemb)
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
	if (match == NULL || n >= match->nmatches)
		return NULL;
	return match->matches[n];
}

static const char *
match_interpolate(const struct match *match)
{
	static char buf[PATH_MAX];
	const char *path, *sub;
	char *end;
	unsigned long mid;
	size_t i = 0;
	size_t j = 0;

	assert(match != NULL);
	assert(match->str != NULL);

	path = match->str;
	while (path[i] != '\0') {
		if (i > 0 && path[i - 1] == '\\' && isdigit(path[i])) {
			errno = 0;
			mid = strtoul(path + i, &end, 10);
			if ((errno == ERANGE && mid == ULONG_MAX) ||
			    ((sub = match_get(match, mid)) == NULL))
				goto err2;
			/* Adjust j to remove previously copied backslash. */
			j--;
			for (; *sub != '\0'; sub++) {
				if (j == sizeof(buf) - 1)
					goto err1;
				buf[j++] = *sub;
			}
			i = end - path;
			continue;
		}
		if (j == sizeof(buf) - 1)
			goto err1;
		buf[j++] = path[i++];
	}
	assert(j < sizeof(buf));
	buf[j] = '\0';
	return buf;

err1:
	warnx("%s: destination too long", path);
	return NULL;
err2:
	warnx("%s: invalid back-reference in destination", path);
	return NULL;
	return 0;
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
