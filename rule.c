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

enum rule_match_type {
	RULE_MATCH_TYPE_BODY = 1,
	RULE_MATCH_TYPE_HEADER,
};

struct rule_match {
	enum rule_match_type type;
	char **matches;
	size_t nmatches;

	/* Used by rule_match_str(). */
	const char *key;
	const char *val;
	size_t valbeg;
	size_t valend;
};

struct rule {
	enum rule_type type;
	char *dest;
	struct rule_match match;

	TAILQ_HEAD(, expr) expressions;
};

struct expr {
	enum expr_type type;
	int negate;
	regex_t pattern;
	regmatch_t *matches;
	size_t nmatches;

	TAILQ_HEAD(, header) headers;
	TAILQ_ENTRY(expr) entry;
};

struct header {
	char *key;

	TAILQ_ENTRY(header) entry;
};

static int expr_eval(struct expr *, struct rule_match *,
    const struct message *);
static int expr_eval_body(struct expr *, struct rule_match *,
    const struct message *);
static int expr_eval_header(struct expr *, struct rule_match *,
    const struct message *);
static int expr_eval_new(struct expr *, struct rule_match *,
    const struct message *);

static int rule_get_type(const struct rule *);

static void rule_match_copy(struct rule_match *, const char *, regmatch_t *,
    size_t);
static char *rule_match_str_body(const struct rule_match *);
static char *rule_match_str_header(const struct rule_match *);

struct rule *
rule_alloc(void)
{
	struct rule *rl;

	rl = calloc(1, sizeof(*rl));
	if (rl == NULL)
		err(1, NULL);
	rl->type = 0;
	TAILQ_INIT(&rl->expressions);

	return rl;
}
void
rule_free(struct rule *rl)
{
	struct expr *ex;
	struct header *hdr;

	if (rl == NULL)
		return;

	while ((ex = TAILQ_FIRST(&rl->expressions)) != NULL) {
		TAILQ_REMOVE(&rl->expressions, ex, entry);
		while ((hdr = TAILQ_FIRST(&ex->headers)) != NULL) {
			TAILQ_REMOVE(&ex->headers, hdr, entry);
			free(hdr);
		}
		regfree(&ex->pattern);
		free(ex->matches);
		free(ex);
	}
	free(rl);
}


void
rule_add_expr(struct rule *rl, struct expr *ex)
{
	TAILQ_INSERT_TAIL(&rl->expressions, ex, entry);
}


int
rule_set_type(struct rule *rl, enum rule_type type)
{
	/* Do not allow the type to change if already initialized. */
	if (rl->type > 0 && rl->type != type)
		return 1;
	rl->type = type;
	return 0;
}

void
rule_set_dest(struct rule *rl, const char *path)
{
	rl->dest = strdup(path);
	if (rl->dest == NULL)
		err(1, NULL);
}

const char *
rule_get_dest(const struct rule *rl)
{
	return rl->dest;
}

struct rule_match *
rule_eval(struct rule *rl, const struct message *msg)
{
	struct expr *ex;
	int res = 1;

	rule_match_free(&rl->match);
	TAILQ_FOREACH(ex, &rl->expressions, entry) {
		if ((res = expr_eval(ex, &rl->match, msg)) == 0) {
			if (rule_get_type(rl) == RULE_TYPE_OR)
				break; /* match, short-circuit OR */
		} else if (rule_get_type(rl) == RULE_TYPE_AND) {
			break; /* no match, short-circuit AND */
		}
	}
	if (res)
		return NULL;
	return &rl->match;
}

void
rule_match_free(struct rule_match *match)
{
	size_t i;

	if (match == NULL)
		return;

	for (i = 0; i < match->nmatches; i++)
		free(match->matches[i]);
	free(match->matches);
	/* Do not free match since it's stored inside its corresponding rule. */
	memset(match, 0, sizeof(*match));
}

const char *
rule_match_get(const struct rule_match *match, unsigned long n)
{
	if (n >= match->nmatches)
		return NULL;
	return match->matches[n];
}

char *
rule_match_str(const struct rule_match *match)
{
	switch (match->type) {
	case RULE_MATCH_TYPE_BODY:
		return rule_match_str_body(match);
	case RULE_MATCH_TYPE_HEADER:
		return rule_match_str_header(match);
	}
	return NULL;
}

struct expr *
expr_alloc(enum expr_type type)
{
	struct expr *ex;

	ex = calloc(1, sizeof(*ex));
	if (ex == NULL)
		err(1, NULL);
	ex->type = type;
	TAILQ_INIT(&ex->headers);
	return ex;
}

void
expr_set_header_key(struct expr *ex, const char *key)
{
	struct header *hdr;
	size_t len;

	assert(ex->type == EXPR_TYPE_HEADER);

	len = strlen(key) + 1;
	hdr = malloc(sizeof(*hdr) + len);
	if (hdr == NULL)
		err(1, NULL);
	hdr->key = (char *)(hdr + 1);
	memcpy(hdr->key, key, len);
	TAILQ_INSERT_TAIL(&ex->headers, hdr, entry);
}

void
expr_set_negate(struct expr *ex, int negate)
{
	ex->negate = negate;
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

static int
expr_eval(struct expr *ex, struct rule_match *match, const struct message *msg)
{
	int res = 1;

	switch (ex->type) {
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
	if (ex->negate)
		res = !res;
	return res;
}

static int
expr_eval_body(struct expr *ex, struct rule_match *match,
    const struct message *msg)
{
	const char *body;

	assert(ex->nmatches > 0);

	body = message_get_body(msg);
	if (body == NULL)
		return 1;
	if (regexec(&ex->pattern, body, ex->nmatches, ex->matches, 0))
		return 1;

	/* Do not override matches for a previous expression. */
	if (match->nmatches > 0)
		return 0;

	match->type = RULE_MATCH_TYPE_BODY;
	match->key = NULL;
	match->val = body;
	match->valbeg = ex->matches[0].rm_so;
	match->valend = ex->matches[0].rm_eo;
	rule_match_copy(match, body, ex->matches, ex->nmatches);

	return 0;
}

static int
expr_eval_header(struct expr *ex, struct rule_match *match,
    const struct message *msg)
{
	const char *val;
	const struct header *hdr;

	assert(ex->nmatches > 0);

	TAILQ_FOREACH(hdr, &ex->headers, entry) {
		val = message_get_header(msg, hdr->key);
		if (val == NULL)
			continue;
		if (regexec(&ex->pattern, val, ex->nmatches, ex->matches, 0))
			continue;

		/* Do not override matches for a previous expression. */
		if (match->nmatches > 0)
			return 0;

		match->type = RULE_MATCH_TYPE_HEADER;
		match->key = hdr->key;
		match->val = val;
		match->valbeg = ex->matches[0].rm_so;
		match->valend = ex->matches[0].rm_eo;
		rule_match_copy(match, val, ex->matches, ex->nmatches);
		return 0;
	}
	return 1;
}

static int
expr_eval_new(struct expr *ex __attribute__((__unused__)),
    struct rule_match *match __attribute__((__unused__)),
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

static int
rule_get_type(const struct rule *rl)
{
	/* Interpret an uninitialized rule as AND. */
	if (rl->type == 0)
		return RULE_TYPE_AND;
	return rl->type;
}

static void
rule_match_copy(struct rule_match *match, const char *str, regmatch_t *src,
    size_t nmemb)
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

static char *
rule_match_str_body(const struct rule_match *match)
{
	const char *beg, *end, *p;
	char *buf;
	size_t size;
	int len, n, padbeg, padend;

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
		return NULL;
	padbeg = match->valbeg - (beg - match->val);
	padend = match->valend - match->valbeg;
	if (padend >= 2)
		padend -= 2;

	len = end - beg;
	/* '\n' + '\0' = 2 */
	size = 2 * (len + 2);
	buf = malloc(size);
	if (buf == NULL)
		err(1, NULL);
	n = snprintf(buf, size, "%.*s\n%*s^%*s$\n",
	    len, beg, padbeg, "", padend, "");
	if (n == -1 || (size_t)n >= size)
		errx(1, "%s: buffer too small", __func__);
	return buf;
}

static char *
rule_match_str_header(const struct rule_match *match)
{
	const char *p, *tmp;
	char *buf;
	size_t size;
	int lenval, n, padbeg, padend;

	/* ':' + ' ' + '\n' + '\0' = 4 */
	size = 2 * (strlen(match->key) + strlen(match->val) + 4);
	buf = malloc(size);
	if (buf == NULL)
		err(1, NULL);

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

	n = snprintf(buf, size, "%s: %.*s\n%*s^%*s$\n",
	    match->key, lenval, match->val, padbeg, "", padend, "");
	if (n == -1 || (size_t)n >= size)
		errx(1, "%s: buffer too small", __func__);
	return buf;
}
