#include <sys/queue.h>

#include <assert.h>
#include <err.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

struct rule_match {
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
static int expr_header_eval(struct expr *, struct rule_match *,
    const struct message *);
static int expr_new_eval(struct expr *, struct rule_match *,
    const struct message *);
static int rule_get_type(const struct rule *);

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
	if (res == 0)
		return &rl->match;
	return NULL;
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
	const char *p;
	char *buf;
	size_t size;
	int lenval, n, padbeg, padend;

	if (match->nmatches == 0)
		return NULL;

	/* ':' + ' ' + '\n' + '\0' = 4 */
	size = 2 * (strlen(match->key) + strlen(match->val) + 4);
	buf = malloc(size);
	if (buf == NULL)
		err(1, NULL);

	/*
	 * Normalize initial padding if the match does not reside on the first
	 * line.
	 */
	p = memrchr(match->val, '\n', match->valbeg);
	if (p != NULL) {
		p++; /* consume '\n' */
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

	len = strlen(key) + 1;
	hdr = malloc(sizeof(*hdr) + len);
	if (hdr == NULL)
		err(1, NULL);
	hdr->key = (char *)(hdr + 1);
	if (strlcpy(hdr->key, key, len) >= len)
		errx(1, "%s: buffer too small", __func__);
	TAILQ_INSERT_TAIL(&ex->headers, hdr, entry);
}

int
expr_set_header_pattern(struct expr *ex, const char *pattern, int flags,
    const char **errstr)
{
	static char buf[1024];
	int ret;
	int rflags = REG_EXTENDED;

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

void
expr_set_negate(struct expr *ex, int negate)
{
	ex->negate = negate;
}

static int
expr_eval(struct expr *ex, struct rule_match *match, const struct message *msg)
{
	int res = -1;

	switch (ex->type) {
	case EXPR_TYPE_HEADER:
		res = expr_header_eval(ex, match, msg);
		break;
	case EXPR_TYPE_NEW:
		res = expr_new_eval(ex, match, msg);
		break;

	/*
	 * Conscious decision to leave out the default case, any decent compiler
	 * will warn if the switch is not exhaustive.
	 */
	}
	if (res == -1)
		errx(1, "%s: %d: unknown expr type", __func__, ex->type);
	if (ex->negate)
		res = !res;
	return res;
}

static int expr_header_eval(struct expr *ex, struct rule_match *match,
    const struct message *msg)
{
	const char *val;
	const struct header *hdr;
	char *str;
	size_t i, len;

	TAILQ_FOREACH(hdr, &ex->headers, entry) {
		val = message_get_header(msg, hdr->key);
		if (val == NULL)
			continue;
		if (regexec(&ex->pattern, val, ex->nmatches, ex->matches, 0))
			continue;

		/* Do not override matches for a previous expression. */
		if (match->nmatches > 0)
			return 0;

		match->key = hdr->key;
		match->val = val;
		match->valbeg = ex->matches[0].rm_so;
		match->valend = ex->matches[0].rm_eo;

		match->matches = reallocarray(NULL, ex->nmatches,
		    sizeof(*match->matches));
		if (match->matches == NULL)
			err(1, NULL);
		match->nmatches = ex->nmatches;
		for (i = 0; i < ex->nmatches; i++) {
			len = ex->matches[i].rm_eo - ex->matches[i].rm_so;
			str = strndup(val + ex->matches[i].rm_so, len);
			if (str == NULL)
				err(1, NULL);
			match->matches[i] = str;
		}
		return 0;
	}
	return 1;
}

static int expr_new_eval(__unused struct expr *ex,
    __unused struct rule_match *match, const struct message *msg)
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
