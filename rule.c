#include "config.h"

#ifdef HAVE_QUEUE
#  include <sys/queue.h>
#else
#  include "compat-queue.h"
#endif

#include <assert.h>
#include <err.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

struct rule {
	char *dest;
	struct expr *expr;
	int cookie;
};

struct expr {
	enum expr_type type;
	int cookie;

	regex_t pattern;
	regmatch_t *matches;
	size_t nmatches;

	struct expr_headers *headers;
	struct match *match;

	struct expr *lhs;
	struct expr *rhs;
};

TAILQ_HEAD(expr_headers, header);

struct header {
	char *key;

	TAILQ_ENTRY(header) entry;
};

struct match {
	char **matches;
	size_t nmatches;

	/* Used by rule_inspect(). */
	const char *key;
	const char *val;
	size_t valbeg;
	size_t valend;
};

static int expr_eval(struct expr *, const struct match **match,
    const struct message *, int);
static int expr_eval_body(struct expr *, const struct match **match,
    const struct message *);
static int expr_eval_header(struct expr *, const struct match **match,
    const struct message *);
static int expr_eval_new(struct expr *, const struct match **match,
    const struct message *);
static void expr_free(struct expr *);
static void expr_inspect(const struct expr *, FILE *, int);
static void expr_inspect_body(const struct expr *, FILE *);
static void expr_inspect_header(const struct expr *, FILE *);

static void match_copy(struct match *, const char *, regmatch_t *,
    size_t);
static void match_free(struct match *match);

struct rule *
rule_alloc(struct expr *ex, const char *dest)
{
	struct rule *rl;

	rl = calloc(1, sizeof(*rl));
	if (rl == NULL)
		err(1, NULL);
	rl->expr = ex;
	rl->dest = strdup(dest);
	if (rl->dest == NULL)
		err(1, NULL);

	return rl;
}
void
rule_free(struct rule *rl)
{
	if (rl == NULL)
		return;

	expr_free(rl->expr);
	free(rl->dest);
	free(rl);
}

void
rule_inspect(const struct rule *rl, FILE *fh)
{
	expr_inspect(rl->expr, fh, rl->cookie);
}

const char *
rule_get_dest(const struct rule *rl)
{
	return rl->dest;
}

int
rule_eval(struct rule *rl, const struct match **match,
    const struct message *msg)
{
	*match = NULL;
	rl->cookie++;
	if (expr_eval(rl->expr, match, msg, rl->cookie))
		return 1;
	return 0;
}

const char *
match_get(const struct match *match, unsigned long n)
{
	if (match == NULL || n >= match->nmatches)
		return NULL;
	return match->matches[n];
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
	if (ex->type == EXPR_TYPE_BODY || ex->type == EXPR_TYPE_HEADER) {
		ex->match = calloc(1, sizeof(*ex->match));
		if (ex->match == NULL)
			err(1, NULL);
	}
	return ex;
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
expr_headers_append(struct expr_headers *headers, const char *key)
{
	struct header *hdr;
	size_t len;

	len = strlen(key) + 1;
	hdr = malloc(sizeof(*hdr) + len);
	if (hdr == NULL)
		err(1, NULL);
	hdr->key = (char *)(hdr + 1);
	memcpy(hdr->key, key, len);
	TAILQ_INSERT_TAIL(headers, hdr, entry);
}

static int
expr_eval(struct expr *ex, const struct match **match,
    const struct message *msg, int cookie)
{
	int res = 1;

	switch (ex->type) {
	case EXPR_TYPE_AND:
		res = expr_eval(ex->lhs, match, msg, cookie);
		if (res)
			break; /* no match, short-circuit */
		res = expr_eval(ex->rhs, match, msg, cookie);
		break;
	case EXPR_TYPE_OR:
		res = expr_eval(ex->lhs, match, msg, cookie);
		if (res == 0)
			break; /* match, short-circuit */
		res = expr_eval(ex->rhs, match, msg, cookie);
		break;
	case EXPR_TYPE_NEG:
		assert(ex->rhs == NULL);
		res = !expr_eval(ex->lhs, match, msg, cookie);
		break;
	case EXPR_TYPE_BODY:
		res = expr_eval_body(ex, match, msg);
		break;
	case EXPR_TYPE_HEADER:
		res = expr_eval_header(ex, match, msg);
		break;
	case EXPR_TYPE_NEW:
		res = expr_eval_new(ex, match, msg);
		break;
	}
	/* Mark expression as visited on match. */
	if (res == 0)
		ex->cookie = cookie;

	return res;
}

static int
expr_eval_body(struct expr *ex, const struct match **match,
    const struct message *msg)
{
	const char *body;

	assert(ex->nmatches > 0);

	body = message_get_body(msg);
	if (body == NULL)
		return 1;
	if (regexec(&ex->pattern, body, ex->nmatches, ex->matches, 0))
		return 1;

	match_free(ex->match);
	ex->match->key = NULL;
	ex->match->val = body;
	ex->match->valbeg = ex->matches[0].rm_so;
	ex->match->valend = ex->matches[0].rm_eo;
	if (*match == NULL) {
		/* First matching expression. */
		match_copy(ex->match, body, ex->matches, ex->nmatches);
		*match = ex->match;
	}
	return 0;
}

static int
expr_eval_header(struct expr *ex, const struct match **match,
    const struct message *msg)
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

		match_free(ex->match);
		ex->match->key = hdr->key;
		ex->match->val = val;
		ex->match->valbeg = ex->matches[0].rm_so;
		ex->match->valend = ex->matches[0].rm_eo;
		if (*match == NULL) {
			/* First matching expression. */
			match_copy(ex->match, val, ex->matches, ex->nmatches);
			*match = ex->match;
		}
		return 0;
	}
	return 1;
}

static int
expr_eval_new(struct expr *ex __attribute__((__unused__)),
    const struct match **match __attribute__((__unused__)),
    const struct message *msg)
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
	regfree(&ex->pattern);
	if (ex->headers != NULL) {
		while ((hdr = TAILQ_FIRST(ex->headers)) != NULL) {
			TAILQ_REMOVE(ex->headers, hdr, entry);
			free(hdr);
		}
		free(ex->headers);
	}
	match_free(ex->match);
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
	const char *p, *tmp;
	int lenval, padbeg, padend;

	match = ex->match;

	/*
	 * Normalize initial padding if the match does not reside on the first
	 * line.
	 */
	p = match->val;
	for (;;) {
		if ((tmp = strchr(p, '\n')) == NULL ||
		    tmp > match->val + match->valbeg)
			break;
		p = tmp + 1;
	}
	if (p != match->val) {
		padbeg = match->valbeg - (p - match->val);
		for (; *p == '\t'; p++)
			padbeg += 7;
	} else {
		padbeg = strlen(match->key) + 2 + match->valbeg;
	}
	padend = match->valend - match->valbeg;
	if (padend >= 2)
		padend -= 2;

	/* Exclude line(s) after the match. */
	p = strchr(match->val + match->valbeg, '\n');
	if (p != NULL)
		lenval = (int)(p - match->val);
	else
		lenval = strlen(match->val);
	fprintf(fh, "%s: %.*s\n%*s^%*s$\n",
	    match->key, lenval, match->val, padbeg, "", padend, "");
}

static void
match_copy(struct match *match, const char *str, regmatch_t *src, size_t nmemb)
{
	char *cpy;
	size_t i, len;

	match->matches = reallocarray(NULL, nmemb, sizeof(*match->matches));
	if (match->matches == NULL)
		err(1, NULL);
	match->nmatches = nmemb;
	for (i = 0; i < nmemb; i++) {
		len = src[i].rm_eo - src[i].rm_so;
		cpy = strndup(str + src[i].rm_so, len);
		if (str == NULL)
			err(1, NULL);
		match->matches[i] = cpy;
	}
}

static void
match_free(struct match *match)
{
	size_t i;

	if (match == NULL)
		return;

	for (i = 0; i < match->nmatches; i++)
		free(match->matches[i]);
	free(match->matches);
	/* Do not free match since it's owned by its expression. */
	memset(match, 0, sizeof(*match));
}
