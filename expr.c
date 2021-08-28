#include "config.h"

#include <sys/stat.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "extern.h"

static int	expr_eval_all(struct expr *, struct expr_eval_arg *);
static int	expr_eval_and(struct expr *, struct expr_eval_arg *);
static int	expr_eval_attachment(struct expr *, struct expr_eval_arg *);
static int	expr_eval_attachment_block(struct expr *,
    struct expr_eval_arg *);
static int	expr_eval_block(struct expr *, struct expr_eval_arg *);
static int	expr_eval_body(struct expr *, struct expr_eval_arg *);
static int	expr_eval_break(struct expr *, struct expr_eval_arg *);
static int	expr_eval_date(struct expr *, struct expr_eval_arg *);
static int	expr_eval_discard(struct expr *, struct expr_eval_arg *);
static int	expr_eval_exec(struct expr *, struct expr_eval_arg *);
static int	expr_eval_flag(struct expr *, struct expr_eval_arg *);
static int	expr_eval_header(struct expr *, struct expr_eval_arg *);
static int	expr_eval_label(struct expr *, struct expr_eval_arg *);
static int	expr_eval_match(struct expr *, struct expr_eval_arg *);
static int	expr_eval_move(struct expr *, struct expr_eval_arg *);
static int	expr_eval_neg(struct expr *, struct expr_eval_arg *);
static int	expr_eval_new(struct expr *, struct expr_eval_arg *);
static int	expr_eval_old(struct expr *, struct expr_eval_arg *);
static int	expr_eval_or(struct expr *, struct expr_eval_arg *);
static int	expr_eval_pass(struct expr *, struct expr_eval_arg *);
static int	expr_eval_reject(struct expr *, struct expr_eval_arg *);
static int	expr_eval_stat(struct expr *, struct expr_eval_arg *);

static int	expr_inspect_prefix(const struct expr *,
    const struct environment *);
static int	expr_match(struct expr *, struct match_list *,
    struct message *);
static int	expr_regexec(struct expr *, struct match_list *,
    struct message *, const struct environment *, const char *, const char *);

static size_t	strnwidth(const char *, size_t);

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
	case EXPR_TYPE_MATCH:
		ex->ex_eval = &expr_eval_match;
		break;
	case EXPR_TYPE_ALL:
		ex->ex_eval = &expr_eval_all;
		break;
	case EXPR_TYPE_ATTACHMENT:
		ex->ex_eval = &expr_eval_attachment;
		break;
	case EXPR_TYPE_BODY:
		ex->ex_eval = &expr_eval_body;
		ex->ex_flags = EXPR_FLAG_INSPECT | EXPR_FLAG_INTERPOLATE;
		break;
	case EXPR_TYPE_DATE:
		ex->ex_eval = &expr_eval_date;
		ex->ex_flags = EXPR_FLAG_INSPECT;
		break;
	case EXPR_TYPE_HEADER:
		ex->ex_eval = &expr_eval_header;
		ex->ex_flags = EXPR_FLAG_INSPECT | EXPR_FLAG_INTERPOLATE;
		break;
	case EXPR_TYPE_NEW:
		ex->ex_eval = &expr_eval_new;
		break;
	case EXPR_TYPE_OLD:
		ex->ex_eval = &expr_eval_old;
		break;
	case EXPR_TYPE_STAT:
		ex->ex_eval = &expr_eval_stat;
		break;
	case EXPR_TYPE_MOVE:
		ex->ex_eval = &expr_eval_move;
		ex->ex_flags = EXPR_FLAG_ACTION | EXPR_FLAG_PATH;
		break;
	case EXPR_TYPE_FLAG:
		ex->ex_eval = &expr_eval_flag;
		ex->ex_flags = EXPR_FLAG_ACTION | EXPR_FLAG_PATH;
		break;
	case EXPR_TYPE_DISCARD:
		ex->ex_eval = &expr_eval_discard;
		ex->ex_flags = EXPR_FLAG_ACTION;
		ex->ex_label = "<discard>";
		break;
	case EXPR_TYPE_BREAK:
		ex->ex_eval = &expr_eval_break;
		ex->ex_flags = EXPR_FLAG_ACTION;
		break;
	case EXPR_TYPE_LABEL:
		ex->ex_eval = &expr_eval_label;
		ex->ex_flags = EXPR_FLAG_ACTION | EXPR_FLAG_PATH;
		ex->ex_label = "<label>";
		break;
	case EXPR_TYPE_PASS:
		ex->ex_eval = &expr_eval_pass;
		ex->ex_flags = EXPR_FLAG_ACTION;
		break;
	case EXPR_TYPE_REJECT:
		ex->ex_eval = &expr_eval_reject;
		ex->ex_flags = EXPR_FLAG_ACTION;
		ex->ex_label = "<reject>";
		break;
	case EXPR_TYPE_EXEC:
		ex->ex_eval = &expr_eval_exec;
		ex->ex_flags = EXPR_FLAG_ACTION;
		ex->ex_label = "<exec>";
		break;
	case EXPR_TYPE_ATTACHMENT_BLOCK:
		ex->ex_eval = &expr_eval_attachment_block;
		break;
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
	regfree(&ex->ex_re.r_pattern);
	free(ex->ex_re.r_matches);
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

	/* Cheat a bit by adding a match all pattern used during dry run. */
	(void)expr_set_pattern(ex, ".*", 0, NULL);
}

void
expr_set_stat(struct expr *ex, char *path, enum expr_stat stat)
{
	struct string_list *strings;

	assert(ex->ex_type == EXPR_TYPE_STAT);

	strings = strings_alloc();
	strings_append(strings, path);
	expr_set_strings(ex, strings);
	ex->ex_stat.s_stat = stat;
}

int
expr_set_exec(struct expr *ex, struct string_list *cmd, unsigned int flags)
{
	if ((flags & (EXPR_EXEC_STDIN | EXPR_EXEC_BODY)) == EXPR_EXEC_BODY)
		return 1;

	expr_set_strings(ex, cmd);
	ex->ex_exec.e_flags = flags;
	return 0;
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
	struct {
		unsigned int	eflag;	/* expr pattern flag */
		unsigned int	pflag;	/* propagate expr pattern flag */
		unsigned int	rflag;	/* regcomp() flag(s) */
	} fflags[] = {
		{ EXPR_PATTERN_ICASE,	0,	REG_ICASE },
		{ EXPR_PATTERN_LCASE,	1,	0 },
		{ EXPR_PATTERN_UCASE,	1,	0 },

		{ 0,	0,	0 },
	};
	int error, i;
	int rflags = REG_EXTENDED | REG_NEWLINE;

	assert(ex->ex_re.r_nmatches == 0);

	for (i = 0; fflags[i].eflag != 0; i++) {
		if ((flags & fflags[i].eflag) == 0)
			continue;

		if (fflags[i].pflag)
			ex->ex_re.r_flags |= fflags[i].eflag;
		if (fflags[i].rflag)
			rflags |= fflags[i].rflag;
		flags &= ~fflags[i].eflag;
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
expr_eval(struct expr *ex, struct expr_eval_arg *ea)
{
	return ex->ex_eval(ex, ea);
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
 * Writes a human readable representation of the latest match to fh.
 */
void
expr_inspect(const struct expr *ex, const struct match *mh,
    const struct environment *env)
{
	const char *lbeg, *lend, *p;
	unsigned int i;
	int len, indent, pindent;
	int printkey = 1;

	if ((ex->ex_flags & EXPR_FLAG_INSPECT) == 0)
		return;

	pindent = strlen(mh->mh_key) + 2;

	for (i = 0; i < mh->mh_nmatches; i++) {
		int beg, end;

		beg = mh->mh_matches[i].m_beg;
		end = mh->mh_matches[i].m_end;
		if (beg == end)
			continue;

		lbeg = mh->mh_val;
		for (;;) {
			if ((p = strchr(lbeg, '\n')) == NULL ||
			    p > mh->mh_val + beg)
				break;
			lbeg = p + 1;
		}
		lbeg += nspaces(lbeg);
		lend = strchr(lbeg, '\n');
		if (lend == NULL)
			lend = mh->mh_val + strlen(mh->mh_val);

		len = strnwidth(mh->mh_val + beg, end - beg);
		/* Try to compensate for the "^$" markers. */
		if (len >= 2)
			len -= 2;
		else
			len = 0;

		if (printkey) {
			pindent += expr_inspect_prefix(ex, env);
			printkey = 0;
			fprintf(stdout, "%s: ", mh->mh_key);
		} else {
			fprintf(stdout, "%*s", pindent, "");
		}
		indent = beg - (lbeg - mh->mh_val) + pindent;
		fprintf(stdout, "%.*s\n%*s^%*s$\n",
		    (int)(lend - lbeg), lbeg, indent, "", len, "");
	}
}

static int
expr_eval_all(struct expr *UNUSED(ex), struct expr_eval_arg *UNUSED(ea))
{
	return EXPR_MATCH;
}

static int
expr_eval_and(struct expr *ex, struct expr_eval_arg *ea)
{
	int ev;

	if ((ev = expr_eval(ex->ex_lhs, ea)) != EXPR_MATCH)
		return ev; /* no match or error, short-circuit */
	return expr_eval(ex->ex_rhs, ea);
}

static int
expr_eval_attachment(struct expr *ex, struct expr_eval_arg *ea)
{
	struct message_list *attachments;
	struct message *msg = ea->ea_msg;
	struct message *attach;

	attachments = message_get_attachments(msg);
	if (attachments == NULL)
		return EXPR_ERROR;

	TAILQ_FOREACH(attach, attachments, me_entry) {
		int ev;

		ea->ea_msg = attach;
		ev = expr_eval(ex->ex_lhs, ea);
		ea->ea_msg = msg;
		if (ev == EXPR_NOMATCH)
			continue;
		return ev;	/* match or error, return */
	}

	return EXPR_NOMATCH;
}

static int
expr_eval_attachment_block(struct expr *ex, struct expr_eval_arg *ea)
{
	struct message_list *attachments;
	struct message *msg = ea->ea_msg;
	struct message *attach;
	int ev = EXPR_NOMATCH;

	attachments = message_get_attachments(msg);
	if (attachments == NULL)
		return EXPR_ERROR;
	if (TAILQ_FIRST(attachments) == NULL)
		return EXPR_NOMATCH;

	TAILQ_FOREACH(attach, attachments, me_entry) {
		int ev2;

		ea->ea_msg = attach;
		ev2 = expr_eval(ex->ex_lhs, ea);
		ea->ea_msg = msg;
		switch (ev2) {
		case EXPR_ERROR:
			return EXPR_ERROR;
		case EXPR_MATCH:
			ev = EXPR_MATCH;
			break;
		}
	}

	return ev;
}

static int
expr_eval_block(struct expr *ex, struct expr_eval_arg *ea)
{
	int ev;

	ev = expr_eval(ex->ex_lhs, ea);
	if (ev == EXPR_ERROR)
		return EXPR_ERROR;

	if (matches_find(ea->ea_ml, EXPR_TYPE_BREAK) != NULL) {
		matches_clear(ea->ea_ml);
		return EXPR_NOMATCH; /* break, continue evaluation */
	}

	if (matches_find(ea->ea_ml, EXPR_TYPE_PASS) != NULL) {
		/*
		 * If removing the pass action results in a match list without
		 * any actions left, we got a pass followed by no effective
		 * action. Therefore treat it as a no match.
		 */
		if (matches_remove(ea->ea_ml, EXPR_TYPE_PASS) == 0)
			return EXPR_NOMATCH;
		return EXPR_MATCH;
	}

	return ev;
}

static int
expr_eval_body(struct expr *ex, struct expr_eval_arg *ea)
{
	const char *body;

	body = message_get_body(ea->ea_msg);
	if (body == NULL)
		return EXPR_ERROR;
	return expr_regexec(ex, ea->ea_ml, ea->ea_msg, ea->ea_env, "Body", body);
}

static int
expr_eval_break(struct expr *ex, struct expr_eval_arg *ea)
{
	struct match *mh;

	mh = match_alloc(ex, ea->ea_msg);
	if (matches_append(ea->ea_ml, mh))
		return EXPR_ERROR;

	/*
	 * Return match in order to continue evaluation. The return value is
	 * later inverted by expr_eval_block().
	 */
	return EXPR_MATCH;
}

static int
expr_eval_date(struct expr *ex, struct expr_eval_arg *ea)
{
	char buf[32];
	const char *date;
	time_t delta, tim;

	if (ex->ex_date.d_field == EXPR_DATE_FIELD_HEADER) {
		date = message_get_header1(ea->ea_msg, "Date");
		if (date == NULL)
			return EXPR_NOMATCH;
		if (time_parse(date, &tim, ea->ea_env))
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
		if (stat(ea->ea_msg->me_path, &st) == -1) {
			warn("stat: %s", ea->ea_msg->me_path);
			return EXPR_ERROR;
		}
		tim = ts->tv_sec;
		date = time_format(tim, buf, sizeof(buf));
		if (date == NULL)
			return EXPR_ERROR;
	}

	delta = ea->ea_env->ev_now - tim;
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
	return expr_regexec(ex, ea->ea_ml, ea->ea_msg, ea->ea_env, "Date", date);
}

static int
expr_eval_exec(struct expr *ex, struct expr_eval_arg *ea)
{
	return expr_match(ex, ea->ea_ml, ea->ea_msg);
}

static int
expr_eval_discard(struct expr *ex, struct expr_eval_arg *ea)
{
	return expr_match(ex, ea->ea_ml, ea->ea_msg);
}

static int
expr_eval_flag(struct expr *ex, struct expr_eval_arg *ea)
{
	struct match *mh;
	const char *subdir;
	size_t siz;

	mh = match_alloc(ex, ea->ea_msg);
	subdir = TAILQ_FIRST(ex->ex_strings)->val;
	siz = sizeof(mh->mh_subdir);
	if (strlcpy(mh->mh_subdir, subdir, siz) >= siz) {
		warnc(ENAMETOOLONG, "%s", __func__);
		match_free(mh);
		return EXPR_ERROR;
	}

	if (matches_append(ea->ea_ml, mh))
		return EXPR_ERROR;
	return EXPR_MATCH;
}

static int
expr_eval_header(struct expr *ex, struct expr_eval_arg *ea)
{
	const struct string_list *values;
	const struct string *key, *val;

	TAILQ_FOREACH(key, ex->ex_strings, entry) {
		values = message_get_header(ea->ea_msg, key->val);
		if (values == NULL)
			continue;

		TAILQ_FOREACH(val, values, entry) {
			int ev;

			ev = expr_regexec(ex, ea->ea_ml, ea->ea_msg, ea->ea_env,
			    key->val, val->val);
			if (ev == EXPR_NOMATCH)
				continue;
			return ev;	/* match or error, return */
		}
	}
	return EXPR_NOMATCH;
}

static int
expr_eval_label(struct expr *ex, struct expr_eval_arg *ea)
{
	return expr_match(ex, ea->ea_ml, ea->ea_msg);
}

static int
expr_eval_match(struct expr *ex, struct expr_eval_arg *ea)
{
	struct match *mh;

	/*
	 * Behaves like and with the exception of adding itself to the match
	 * list. The match is later used by matches_find_interpolate().
	 */
	mh = match_alloc(ex, ea->ea_msg);
	if (matches_append(ea->ea_ml, mh))
		return EXPR_ERROR;

	return expr_eval_and(ex, ea);
}

static int
expr_eval_move(struct expr *ex, struct expr_eval_arg *ea)
{
	struct match *mh;
	const char *maildir;
	size_t siz;

	maildir = TAILQ_FIRST(ex->ex_strings)->val;
	mh = match_alloc(ex, ea->ea_msg);
	siz = sizeof(mh->mh_maildir);
	if (strlcpy(mh->mh_maildir, maildir, siz) >= siz) {
		warnc(ENAMETOOLONG, "%s", __func__);
		match_free(mh);
		return EXPR_ERROR;
	}

	if (matches_append(ea->ea_ml, mh))
		return EXPR_ERROR;
	return EXPR_MATCH;
}

static int
expr_eval_neg(struct expr *ex, struct expr_eval_arg *ea)
{
	assert(ex->ex_rhs == NULL);

	switch (expr_eval(ex->ex_lhs, ea)) {
	case EXPR_ERROR:
		return EXPR_ERROR;
	case EXPR_NOMATCH:
		return EXPR_MATCH;
	}

	/* No match, invalidate match below current expression. */
	matches_clear(ea->ea_ml);
	return EXPR_NOMATCH;
}

static int
expr_eval_new(struct expr *UNUSED(ex), struct expr_eval_arg *ea)
{
	char buf[NAME_MAX + 1];

	if (pathslice(ea->ea_msg->me_path, buf, sizeof(buf), -2, -2) == NULL ||
	    strcmp(buf, "new"))
		return EXPR_NOMATCH;
	return EXPR_MATCH;
}

static int
expr_eval_old(struct expr *UNUSED(ex), struct expr_eval_arg *ea)
{
	char buf[NAME_MAX + 1];

	if (message_flags_isset(&ea->ea_msg->me_mflags, 'S'))
		return EXPR_NOMATCH;
	if (pathslice(ea->ea_msg->me_path, buf, sizeof(buf), -2, -2) == NULL ||
	    strcmp(buf, "cur"))
		return EXPR_NOMATCH;
	return EXPR_MATCH;
}

static int
expr_eval_or(struct expr *ex, struct expr_eval_arg *ea)
{
	int ev;

	if ((ev = expr_eval(ex->ex_lhs, ea)) != EXPR_NOMATCH)
		return ev; /* match or error, short-circuit */
	return expr_eval(ex->ex_rhs, ea);
}

static int
expr_eval_pass(struct expr *ex, struct expr_eval_arg *ea)
{
	struct match *mh;

	mh = match_alloc(ex, ea->ea_msg);
	if (matches_append(ea->ea_ml, mh))
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
expr_eval_reject(struct expr *ex, struct expr_eval_arg *ea)
{
	return expr_match(ex, ea->ea_ml, ea->ea_msg);
}

static int
expr_eval_stat(struct expr *ex, struct expr_eval_arg *ea)
{
	struct stat st;
	struct match *mh;
	const char *str;
	size_t siz;
	int ev = EXPR_NOMATCH;

	mh = match_alloc(ex, ea->ea_msg);
	if (matches_append(ea->ea_ml, mh)) {
		ev = EXPR_ERROR;
		goto out;
	}

	str = TAILQ_FIRST(ex->ex_strings)->val;
	siz = sizeof(mh->mh_path);
	if (strlcpy(mh->mh_path, str, siz) >= siz) {
		warnc(ENAMETOOLONG, "%s", __func__);
		ev = EXPR_ERROR;
	} else if (match_interpolate(mh, NULL)) {
		ev = EXPR_ERROR;
	} else if (stat(mh->mh_path, &st) == 0) {
		switch (ex->ex_stat.s_stat) {
		case EXPR_STAT_DIR:
			if (S_ISDIR(st.st_mode))
				ev = EXPR_MATCH;
			break;
		}
	}

out:
	TAILQ_REMOVE(ea->ea_ml, mh, mh_entry);
	match_free(mh);

	return ev;
}

static int
expr_inspect_prefix(const struct expr *ex, const struct environment *env)
{
	const char *path;
	size_t len;
	int n;
	int nwrite = 0;

	path = env->ev_confpath;
	len = strlen(env->ev_home);
	if (strncmp(path, env->ev_home, len) == 0) {
		n = fprintf(stdout, "~");
		if (n > 0)
			nwrite += n;
		path += len;
	}
	n = fprintf(stdout, "%s:%d: ", path, ex->ex_lno);
	if (n > 0)
		nwrite += n;
	return nwrite;
}

static int
expr_match(struct expr *ex, struct match_list *ml, struct message *msg)
{
	struct match *mh;

	mh = match_alloc(ex, msg);
	if (matches_append(ml, mh))
		return EXPR_ERROR;
	return EXPR_MATCH;
}

static int
expr_regexec(struct expr *ex, struct match_list *ml, struct message *msg,
    const struct environment *env, const char *key, const char *val)
{
	struct match *mh;
	int error;

	error = regexec(&ex->ex_re.r_pattern, val, ex->ex_re.r_nmatches,
	    ex->ex_re.r_matches, 0);
	if (error == REG_NOMATCH)
		return EXPR_NOMATCH;
	if (error != 0)
		return EXPR_ERROR;

	mh = match_alloc(ex, msg);
	match_copy(mh, val, ex->ex_re.r_matches, ex->ex_re.r_nmatches);
	if (matches_append(ml, mh))
		return EXPR_ERROR;

	if (env->ev_options & OPTION_DRYRUN) {
		mh->mh_key = strdup(key);
		if (mh->mh_key == NULL)
			err(1, NULL);
		mh->mh_val = strdup(val);
		if (mh->mh_val == NULL)
			err(1, NULL);
	}

	return EXPR_MATCH;
}

static size_t
strnwidth(const char *str, size_t len)
{
	size_t i;
	size_t width = 0;

	for (i = 0; i < len;) {
		wchar_t wc;
		int n;

		n = mbtowc(&wc, &str[i], MB_CUR_MAX);
		if (n == -1) {
			mbtowc(NULL, NULL, MB_CUR_MAX);
			i++;
			width++;
			continue;
		}
		if (n == 0)
			break;
		if (n > 0)
			i += n;

		n = wcwidth(wc);
		if (n > 0)
			width += n;
	}

	return width;
}
