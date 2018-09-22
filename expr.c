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
	char maildir[PATH_MAX];
	char subdir[NAME_MAX];

	/* Everything after this field will be zeroed out by match_reset(). */
	int begzero;

	char **matches;
	size_t nmatches;

	const char *key;
	const char *val;
	size_t valbeg;
	size_t valend;
};

static int expr_eval1(struct expr *, struct expr *, const struct message *);
static int expr_eval_all(struct expr *, struct expr *, const struct message *);
static int expr_eval_body(struct expr *, struct expr *, const struct message *);
static int expr_eval_flag(struct expr *, struct expr *, const struct message *);
static int expr_eval_header(struct expr *, struct expr *,
    const struct message *);
static int expr_eval_move(struct expr *, struct expr *, const struct message *);
static int expr_eval_new(struct expr *, struct expr *, const struct message *);
static int expr_eval_old(struct expr *, struct expr *, const struct message *);
static void expr_inspect1(const struct expr *, const struct expr *, FILE *);
static void expr_inspect_body(const struct expr *, FILE *);
static void expr_inspect_header(const struct expr *, FILE *);

static void match_copy(struct match *, const char *, const regmatch_t *,
    size_t);
static const char *match_get(const struct match *, unsigned long n);
static char *match_interpolate(const struct match *, char *, size_t);
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
	case EXPR_TYPE_ROOT:
	case EXPR_TYPE_BODY:
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

const char *
expr_eval(struct expr *ex, const struct message *msg)
{
	static char buf[PATH_MAX];
	const char *path = NULL;

	memset(ex->match, 0, sizeof(*ex->match));
	ex->cookie++;
	if (expr_eval1(ex, ex, msg))
		goto done;
	path = match_interpolate(ex->match, buf, sizeof(buf));

done:
	match_reset(ex->match);
	return path;
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

void
expr_inspect(const struct expr *ex, FILE *fh)
{
	expr_inspect1(ex, ex, fh);
}

static int
expr_eval1(struct expr *root, struct expr *ex, const struct message *msg)
{
	int res = 1;

	switch (ex->type) {
	case EXPR_TYPE_ROOT:
		res = expr_eval1(root, ex->lhs, msg);
		break;
	case EXPR_TYPE_AND:
		res = expr_eval1(root, ex->lhs, msg);
		if (res)
			break; /* no match, short-circuit */
		res = expr_eval1(root, ex->rhs, msg);
		break;
	case EXPR_TYPE_OR:
		res = expr_eval1(root, ex->lhs, msg);
		if (res == 0)
			break; /* match, short-circuit */
		res = expr_eval1(root, ex->rhs, msg);
		break;
	case EXPR_TYPE_NEG:
		assert(ex->rhs == NULL);
		res = !expr_eval1(root, ex->lhs, msg);
		/* On non-match, invalidate match below expression. */
		if (res)
			match_reset(root->match);
		break;
	case EXPR_TYPE_ALL:
		res = expr_eval_all(root, ex, msg);
		break;
	case EXPR_TYPE_BODY:
		res = expr_eval_body(root, ex, msg);
		break;
	case EXPR_TYPE_HEADER:
		res = expr_eval_header(root, ex, msg);
		break;
	case EXPR_TYPE_NEW:
		res = expr_eval_new(root, ex, msg);
		break;
	case EXPR_TYPE_OLD:
		res = expr_eval_old(root, ex, msg);
		break;
	case EXPR_TYPE_MOVE:
		res = expr_eval_move(root, ex, msg);
		break;
	case EXPR_TYPE_FLAG:
		res = expr_eval_flag(root, ex, msg);
		break;
	}
	if (res == 0) {
		/* Mark expression as visited on match. */
		ex->cookie = root->cookie;
	}

	return res;
}

static int
expr_eval_all(struct expr *root __attribute__((__unused__)),
    struct expr *ex __attribute__((__unused__)),
    const struct message *msg __attribute__((__unused__)))
{
	return 0;
}

static int
expr_eval_body(struct expr *root, struct expr *ex, const struct message *msg)
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
	match_copy(root->match, body, ex->matches, ex->nmatches);
	return 0;
}

static int
expr_eval_flag(struct expr *root, struct expr *ex, const struct message *msg)
{
	struct string *str;
	const char *path;
	size_t len;

	str = TAILQ_FIRST(ex->strings);
	len = sizeof(root->match->subdir);
	if (strlcpy(root->match->subdir, str->val, len) >= len)
		errx(1, "%s: buffer too small", __func__);

	/* A move action might be missing. */
	if (strlen(root->match->maildir) == 0) {
		path = message_get_path(msg);
		if (pathslice(path, root->match->maildir, 0, -2) == NULL)
			errx(1, "%s: %s: maildir not found", __func__, path);
	}

	return 0;
}

static int
expr_eval_header(struct expr *root, struct expr *ex, const struct message *msg)
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
			if (regexec(&ex->pattern, val->val, ex->nmatches,
				    ex->matches, 0))
				continue;

			match_reset(ex->match);
			ex->match->key = key->val;
			ex->match->val = val->val;
			ex->match->valbeg = ex->matches[0].rm_so;
			ex->match->valend = ex->matches[0].rm_eo;
			match_copy(root->match, val->val, ex->matches,
			    ex->nmatches);
			return 0;
		}
	}
	return 1;
}

static int
expr_eval_move(struct expr *root, struct expr *ex, const struct message *msg)
{
	struct string *str;
	const char *path;
	size_t len;

	str = TAILQ_FIRST(ex->strings);
	len = sizeof(root->match->maildir);
	if (strlcpy(root->match->maildir, str->val, len) >= len)
		errx(1, "%s: buffer too small", __func__);

	/* A flag action might already have been evaluted. */
	if (strlen(root->match->subdir) == 0) {
		path = message_get_path(msg);
		if (pathslice(path, root->match->subdir, -2, -2) == NULL)
			errx(1, "%s: %s: subdir not found", __func__, path);
	}

	return 0;
}

static int
expr_eval_new(struct expr *root __attribute__((__unused__)),
    struct expr *ex __attribute__((__unused__)), const struct message *msg)
{
	char buf[NAME_MAX];
	const char *path;

	path = message_get_path(msg);
	if (pathslice(path, buf, -2, -2) == NULL || strcmp(buf, "new"))
		return 1;
	return 0;
}

static int
expr_eval_old(struct expr *root __attribute__((__unused__)),
    struct expr *ex __attribute__((__unused__)), const struct message *msg)
{
	char buf[NAME_MAX];
	const char *path;

	if (message_has_flags(msg, 'S'))
		return 1;
	path = message_get_path(msg);
	if (pathslice(path, buf, -2, -2) == NULL || strcmp(buf, "cur"))
		return 1;
	return 0;
}

static void
expr_inspect1(const struct expr *root, const struct expr *ex, FILE *fh)
{
	/* Ensure expression was visited during last call to expr_eval(). */
	if (root->cookie != ex->cookie)
		return;

	switch (ex->type) {
	case EXPR_TYPE_ROOT:
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
	case EXPR_TYPE_HEADER:
		expr_inspect_header(ex, fh);
		break;
	case EXPR_TYPE_NEG:
	case EXPR_TYPE_ALL:
	case EXPR_TYPE_NEW:
	case EXPR_TYPE_OLD:
	case EXPR_TYPE_MOVE:
	case EXPR_TYPE_FLAG:
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

static void
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

static char *
match_interpolate(const struct match *match, char *buf, size_t size)
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
				return NULL;
			}
			for (; *sub != '\0'; sub++) {
				if (j == size - 1)
					goto toolong;
				buf[j++] = *sub;
			}
			i = end - path;
			continue;
		}
		if (j == size - 1)
			goto toolong;
		buf[j++] = path[i++];
	}
	assert(j < size);
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
