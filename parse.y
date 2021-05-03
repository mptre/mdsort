%{

#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "extern.h"

static void expr_validate(const struct expr *);
static void expr_validate_attachment_block(const struct expr *);
static void yyerror(const char *, ...)
	__attribute__((__format__ (printf, 1, 2)));
static int yygetc(void);
static int yylex(void);
static int yypeek(int);
static void yyungetc(int);
static void yyrecover(void);

static void yypushl(int);
static void yypopl(void);

static void macros_free(struct macro_list *);
static void macros_validate(const struct macro_list *);

static char *expand(char *, unsigned int);
static char *expandmacros(char *, struct macro_list *, unsigned int);
static struct string_list *expandstrings(struct string_list *, unsigned int);
static char *expandtilde(char *, const char *);

static struct config_list yyconfig;
static const struct environment *yyenv;
static FILE *yyfh;
static const char *yypath;
static char *stdinpath;
static int lineno, lineno_save, sflag, parse_errors, pflag;

typedef struct {
	union {
		enum expr_date_cmp cmp;
		enum expr_date_field field;
		struct expr *expr;
		unsigned int number;
		struct {
			char *string;
			unsigned int flags;
		} pattern;
		char *string;
		struct string_list *strings;
		time_t time;
	} v;
	int lineno;
} YYSTYPE;

%}

%token ACCESS
%token ALL
%token ATTACHMENT
%token BODY
%token BREAK
%token CREATED
%token DATE
%token DISCARD
%token EXEC
%token FLAG
%token HEADER
%token INT
%token ISDIRECTORY
%token LABEL
%token MACRO
%token MAILDIR
%token MATCH
%token MODIFIED
%token MOVE
%token NEW
%token OLD
%token PASS
%token PATTERN
%token REJECT
%token SCALAR
%token STDIN
%token STRING
%token SYNC

%type <v.cmp>		date_cmp
%type <v.expr>		expr
%type <v.expr>		expr1
%type <v.expr>		expr2
%type <v.expr>		expr3
%type <v.expr>		expraction
%type <v.expr>		expractions
%type <v.expr>		exprblock
%type <v.expr>		exprs
%type <v.field>		date_field
%type <v.number>	INT
%type <v.number>	SCALAR
%type <v.number>	exec_flag
%type <v.number>	exec_flags
%type <v.number>	optneg
%type <v.number>	scalar
%type <v.pattern>	PATTERN
%type <v.pattern>	pattern
%type <v.string>	MACRO
%type <v.string>	STRING
%type <v.string>	flag
%type <v.string>	maildir_path
%type <v.strings>	stringblock
%type <v.strings>	strings
%type <v.time>		date_age

%left AND OR
%left NEG
%left ATTACHMENT

%%

grammar		: /* empty */
		| grammar macro
		| grammar maildir
		| error {
			yyrecover();
		}
		;

macro		: MACRO '=' STRING {
			$3 = expand($3, MACRO_CTX_DEFAULT);
			if (macros_insert(yyconfig.cf_macros, $1, $3, lineno))
				yyerror("macro already defined: %s", $1);
		}
		;

maildir		: maildir_path exprblock {
			struct config *conf;
			unsigned int flags = MAILDIR_WALK;

			/* Favor more specific error messages. */
			if (parse_errors == 0 && expr_count_actions($2) == 0)
				yyerror("empty match block");

			if ($1 == stdinpath)
				flags |= MAILDIR_STDIN;
			else if (expr_count($2, EXPR_TYPE_REJECT) > 0)
				yyerror("reject cannot be used outside stdin");

			conf = malloc(sizeof(*conf));
			if (conf == NULL)
				err(1, NULL);
			conf->maildir.path = $1;
			conf->maildir.flags = flags;
			conf->expr = $2;
			TAILQ_INSERT_TAIL(&yyconfig.cf_list, conf, entry);
		}
		;

maildir_path	: MAILDIR STRING {
			$$ = expand($2, MACRO_CTX_DEFAULT);
		}
		| STDIN {
			const struct config *conf;

			$$ = stdinpath;
			TAILQ_FOREACH(conf, &yyconfig.cf_list, entry)
				if (conf->maildir.flags & MAILDIR_STDIN)
					yyerror("stdin already defined");
		}
		;

exprblock	: '{' exprs '}' {
			$$ = expr_alloc(EXPR_TYPE_BLOCK, lineno, $2, NULL);
		}
		;

exprs		: /* empty */ {
			$$ = NULL;
		}
		| exprs expr {
			if ($1 == NULL)
				$$ = $2;
			else
				$$ = expr_alloc(EXPR_TYPE_OR, lineno, $1, $2);
		}
		| error {
			yyrecover();
			$$ = NULL;
		}
		;

expr		: MATCH expr1 expr2 {
			$$ = expr_alloc(EXPR_TYPE_MATCH, lineno, $2, $3);
		}
		;

expr1		: expr1 AND expr1 {
			$$ = expr_alloc(EXPR_TYPE_AND, lineno, $1, $3);
		}
		| expr1 OR expr1 {
			$$ = expr_alloc(EXPR_TYPE_OR, lineno, $1, $3);
		}
		| ATTACHMENT expr1 {
			$$ = expr_alloc(EXPR_TYPE_ATTACHMENT, lineno, $2, NULL);
		}
		| NEG expr1 {
			$$ = expr_alloc(EXPR_TYPE_NEG, lineno, $2, NULL);
		}
		| expr3
		;

expr2		: expractions {
			expr_validate($$);
		}
		| exprblock {
			$$ = $1;
			if (expr_count_actions($$) == 0)
				yyerror("empty nested match block");
		}
		;

expr3		: BODY pattern {
			const char *errstr;

			$$ = expr_alloc(EXPR_TYPE_BODY, lineno, NULL, NULL);
			if (expr_set_pattern($$, $2.string, $2.flags, &errstr))
				yyerror("invalid pattern: %s", errstr);
		}
		| HEADER strings pattern {
			const char *errstr;

			$$ = expr_alloc(EXPR_TYPE_HEADER, lineno, NULL, NULL);
			if (expr_set_pattern($$, $3.string, $3.flags, &errstr))
				yyerror("invalid pattern: %s", errstr);
			$2 = expandstrings($2, MACRO_CTX_DEFAULT);
			expr_set_strings($$, $2);
		}
		| DATE date_field date_cmp date_age {
			$$ = expr_alloc(EXPR_TYPE_DATE, lineno, NULL, NULL);
			expr_set_date($$, $2, $3, $4);
		}
		| NEW {
			$$ = expr_alloc(EXPR_TYPE_NEW, lineno, NULL, NULL);
		}
		| OLD {
			$$ = expr_alloc(EXPR_TYPE_OLD, lineno, NULL, NULL);
		}
		| ALL {
			$$ = expr_alloc(EXPR_TYPE_ALL, lineno, NULL, NULL);
		}
		| ISDIRECTORY STRING {
			char *path;

			$$ = expr_alloc(EXPR_TYPE_STAT, lineno, NULL, NULL);
			path = expand($2, MACRO_CTX_DEFAULT);
			expr_set_stat($$, path, EXPR_STAT_DIR);
		}
		| '(' expr1 ')' {
			$$ = $2;
		}
		;

expractions	: /* empty */ {
			$$ = NULL;
		}
		| expractions expraction {
			if ($1 == NULL) {
				$$ = $2;
			} else {
				$$ = expr_alloc(EXPR_TYPE_AND, lineno, $1, $2);
			}
		}
		;

expraction	: BREAK {
			$$ = expr_alloc(EXPR_TYPE_BREAK, lineno, NULL, NULL);
		}
		| MOVE STRING {
			struct string_list *strings;
			char *path;

			$$ = expr_alloc(EXPR_TYPE_MOVE, lineno, NULL, NULL);
			path = expand($2, MACRO_CTX_ACTION);
			strings = strings_alloc();
			strings_append(strings, path);
			expr_set_strings($$, strings);
		}
		| FLAG flag {
			struct string_list *strings;

			$$ = expr_alloc(EXPR_TYPE_FLAG, lineno, NULL, NULL);
			strings = strings_alloc();
			strings_append(strings, $2);
			expr_set_strings($$, strings);
		}
		| DISCARD {
			$$ = expr_alloc(EXPR_TYPE_DISCARD, lineno, NULL, NULL);
		}
		| LABEL strings {
			$$ = expr_alloc(EXPR_TYPE_LABEL, lineno, NULL, NULL);
			$2 = expandstrings($2, MACRO_CTX_ACTION);
			expr_set_strings($$, $2);
		}
		| PASS {
			$$ = expr_alloc(EXPR_TYPE_PASS, lineno, NULL, NULL);
		}
		| REJECT {
			$$ = expr_alloc(EXPR_TYPE_REJECT, lineno, NULL, NULL);
		}
		| EXEC exec_flags strings {
			$$ = expr_alloc(EXPR_TYPE_EXEC, lineno, NULL, NULL);
			$3 = expandstrings($3, MACRO_CTX_ACTION);
			if (expr_set_exec($$, $3, $2))
				yyerror("invalid exec options");
		}
		| ATTACHMENT exprblock {
			expr_validate_attachment_block($2);
			$$ = expr_alloc(EXPR_TYPE_ATTACHMENT_BLOCK, lineno, $2,
			    NULL);
		}
		;


strings		: '{' stringblock '}' {
			$$ = $2;
		}
		| STRING {
			$$ = strings_alloc();
			strings_append($$, $1);
		}
		;

stringblock	: /* empty */ {
			$$ = strings_alloc();
		}
		| stringblock STRING {
			$$ = $1;
			strings_append($$, $2);
		}
		;

flag		: optneg NEW {
			if ($1)
				$$ = strdup("cur");
			else
				$$ = strdup("new");
			if ($$ == NULL)
				err(1, NULL);
		}
		;

date_field	: /* empty */ {
			$$ = EXPR_DATE_FIELD_HEADER;
		}
		| HEADER {
			$$ = EXPR_DATE_FIELD_HEADER;
		}
		| ACCESS {
			$$ = EXPR_DATE_FIELD_ACCESS;
		}
		| MODIFIED {
			$$ = EXPR_DATE_FIELD_MODIFIED;
		}
		| CREATED {
			$$ = EXPR_DATE_FIELD_CREATED;
		}
		;

date_cmp	: '<' {
			$$ = EXPR_DATE_CMP_LT;
		}
		| '>' {
			$$ = EXPR_DATE_CMP_GT;
		}
		;

date_age	: INT scalar {
			if ($1 > UINT_MAX / $2) {
				yyerror("integer too large");
				$$ = 0;
			} else {
				$$ = $1 * $2;
			}
		}
		;

scalar		: /* backdoor */ {
			sflag = 1;
		} SCALAR {
			sflag = 0;
			$$ = $2;
		}
		;

exec_flags	: /* empty */ {
			$$ = 0;
		}
		| exec_flags exec_flag {
			if ($1 & $2)
				yyerror("exec options cannot be repeated");
			$$ = $1 | $2;
		}
		;

exec_flag	: STDIN {
			$$ = EXPR_EXEC_STDIN;
		}
		| BODY {
			$$ = EXPR_EXEC_BODY;
		}
		;


pattern		: /* backdoor */ {
			pflag = 1;
		} PATTERN {
			pflag = 0;
			$$ = $2;
		}
		;

optneg		: /* empty */ {
			$$ = 0;
		}
		| NEG {
			$$ = 1;
		}
		;

%%

/*
 * Parses the configuration located at path and returns a config list on
 * success.
 * Otherwise, NULL is returned.
 */
struct config_list *
config_parse(const char *path, const struct environment *env)
{
	yyfh = fopen(path, "r");
	if (yyfh == NULL) {
		warn("%s", path);
		return NULL;
	}
	yypath = path;
	yyenv = env;

	yyconfig.cf_macros = malloc(sizeof(*yyconfig.cf_macros));
	if (yyconfig.cf_macros == NULL)
		err(1, NULL);
	macros_init(yyconfig.cf_macros, MACRO_CTX_DEFAULT);

	TAILQ_INIT(&yyconfig.cf_list);

	lineno = 1;
	lineno_save = -1;
	yyparse();
	fclose(yyfh);
	macros_validate(yyconfig.cf_macros);
	if (parse_errors > 0) {
		config_free(&yyconfig);
		return NULL;
	}
	return &yyconfig;
}

void
config_free(struct config_list *config)
{
	struct config *conf;

	if (config == NULL)
		return;

	while ((conf = TAILQ_FIRST(&config->cf_list)) != NULL) {
		TAILQ_REMOVE(&config->cf_list, conf, entry);
		expr_free(conf->expr);
		free(conf->maildir.path);
		free(conf);
	}

	macros_free(config->cf_macros);
}

static void
yyerror(const char *fmt, ...)
{
	char buf[BUFSIZ];
	char *cp = buf;
	size_t bufsiz = sizeof(buf);
	va_list ap;
	int n;

	n = snprintf(cp, bufsiz, "%s:%d: ", yypath, yylval.lineno);
	if (n > 0 && (size_t)n < bufsiz) {
		cp += n;
		bufsiz -= n;
	}

	va_start(ap, fmt);
	n = vsnprintf(cp, bufsiz, fmt, ap);
	va_end(ap);
	if (n > 0 && (size_t)n < bufsiz) {
		cp += n;
		bufsiz -= n;
	}

	/* Play it safe if any of the above failed. */
	if (bufsiz > 0)
		*cp = '\0';

	fprintf(stderr, "%s\n", buf);

	parse_errors++;
}

static int
yylex(void)
{
	static struct {
		const char *str;
		int type;
	} keywords[] = {
		{ "access",		ACCESS },
		{ "all",		ALL },
		{ "and",		AND },
		{ "attachment",		ATTACHMENT },
		{ "body",		BODY },
		{ "break",		BREAK },
		{ "created",		CREATED },
		{ "date",		DATE },
		{ "discard",		DISCARD },
		{ "exec",		EXEC },
		{ "flag",		FLAG },
		{ "header",		HEADER },
		{ "isdirectory",	ISDIRECTORY },
		{ "label",		LABEL },
		{ "maildir",		MAILDIR },
		{ "match",		MATCH },
		{ "modified",		MODIFIED },
		{ "move",		MOVE },
		{ "new",		NEW },
		{ "old",		OLD },
		{ "or",			OR },
		{ "pass",		PASS },
		{ "reject",		REJECT },
		{ "stdin",		STDIN },

		{ NULL,		0 },
	};
	static struct {
		const char *str;
		unsigned int val;
	} scalars[] = {
		{ "seconds",	1 },
		{ "minutes",	60 },
		{ "hours",	60 * 60 },
		{ "days",	60 * 60 * 24 },
		{ "weeks",	60 * 60 * 24 * 7 },
		{ "months",	60 * 60 * 24 * 30 },
		{ "years",	60 * 60 * 24 * 365 },

		{ NULL,		0 },
	};
	static char lexeme[BUFSIZ];
	static int token_save;
	char *buf;
	int c, i, lno;

	buf = lexeme;
	lno = lineno;

again:
	for (c = yygetc(); isspace((unsigned char)c); c = yygetc())
		continue;

	/*
	 * Macros must always be followed by `=', otherwise treat it as an
	 * unknown keyword.
	 */
	if (token_save == MACRO && c != '=') {
		yypushl(lno);
		yyerror("unknown keyword: %s", lexeme);
		yypopl();
	}

	yylval.lineno = lineno;
	yylval.v.number = c;
	if (c == EOF)
		return (token_save = 0);
	if (c == '!')
		return (token_save = NEG);

	if (c == '#') {
		for (;;) {
			c = yygetc();
			if (c == '\n')
				goto again;
			if (c == EOF)
				return (token_save = 0);
		}
	}

	if (c == '"') {
		size_t len;

		for (;;) {
			if (yypeek('"'))
				break;
			c = yygetc();
			if (c == EOF) {
				yyerror("unterminated string");
				return (token_save = 0);
			}

			if (buf == lexeme + sizeof(lexeme) - 1) {
				yyerror("string too long");
				return (token_save = 0);
			}
			*buf++ = c;
		}
		*buf = '\0';
		len = strlen(lexeme);
		if (len == 0)
			yyerror("empty string");
		yylval.v.string = strdup(lexeme);
		if (yylval.v.string == NULL)
			err(1, NULL);
		return (token_save = STRING);
	}

	if (pflag) {
		unsigned char delim = c;

		for (;;) {
			if (yypeek(delim))
				break;
			c = yygetc();
			if (c == EOF) {
				yyerror("unterminated pattern");
				return (token_save = 0);
			}

			if (buf == lexeme + sizeof(lexeme) - 1) {
				yyerror("pattern too long");
				return (token_save = 0);
			}
			*buf++ = c;
		}
		*buf = '\0';
		yylval.v.pattern.string = lexeme;

		yylval.v.pattern.flags = 0;
		for (;;) {
			c = yygetc();
			switch (c) {
			case 'i':
				yylval.v.pattern.flags |= EXPR_PATTERN_ICASE;
				break;
			case 'l':
				if (yylval.v.pattern.flags & EXPR_PATTERN_UCASE)
					yyerror("`l' and `u' flags cannot be combined");
				yylval.v.pattern.flags |= EXPR_PATTERN_LCASE;
				break;
			case 'u':
				if (yylval.v.pattern.flags & EXPR_PATTERN_LCASE)
					yyerror("`l' and `u' flags cannot be combined");
				yylval.v.pattern.flags |= EXPR_PATTERN_UCASE;
				break;
			default:
				yyungetc(c);
				return (token_save = PATTERN);
			}
		}
	}

	if (isdigit((unsigned char)c)) {
		int overflow = 0;

		yylval.v.number = 0;
		for (; isdigit((unsigned char)c); c = yygetc()) {
			if (overflow)
				continue;

			if (yylval.v.number > UINT_MAX / 10) {
				yyerror("integer too large");
				overflow = 1;
				continue;
			}
			yylval.v.number *= 10;

			if (yylval.v.number > UINT_MAX - (c - '0')) {
				yyerror("integer too large");
				overflow = 1;
				continue;
			}
			yylval.v.number += c - '0';
		}
		yyungetc(c);
		return (token_save = INT);
	}

	if (islower((unsigned char)c)) {
		for (; islower((unsigned char)c); c = yygetc()) {
			if (buf == lexeme + sizeof(lexeme) - 1) {
				yyerror("keyword too long");
				return (token_save = 0);
			}
			*buf++ = c;
		}
		*buf = '\0';
		yyungetc(c);

		for (i = 0; keywords[i].str != NULL; i++)
			if (strcmp(lexeme, keywords[i].str) == 0)
				return (token_save = keywords[i].type);

		if (sflag) {
			size_t len;
			int ambiguous = 0;
			int match = -1;

			len = strlen(lexeme);
			for (i = 0; scalars[i].str != NULL; i++) {
				if (strncmp(lexeme, scalars[i].str, len) == 0) {
					if (match >= 0)
						ambiguous++;
					match = i;
				}
			}
			if (ambiguous) {
				yyerror("ambiguous keyword: %s", lexeme);
				return (token_save = SCALAR);
			} else if (match >= 0) {
				yylval.v.number = scalars[match].val;
				return (token_save = SCALAR);
			}
		}

		yylval.v.string = strdup(lexeme);
		if (yylval.v.string == NULL)
			err(1, NULL);
		/*
		 * At this point, it's unknown if a macro is expected. An error
		 * is emitted upon the next invocation of yylex() if the macro
		 * was unexpected.
		 */
		return (token_save = MACRO);
	}

	return (token_save = c);
}

static void
expr_validate(const struct expr *ex)
{
	int nactions, nflag, nlabel, npass;

	if (ex == NULL) {
		yyerror("missing action");
		return;
	}

	yypushl(ex->ex_lno);

	nactions = expr_count_actions(ex);
	if (nactions > 1) {
		if (expr_count(ex, EXPR_TYPE_BREAK) > 0)
			yyerror("break cannot be combined with another action");
		if (expr_count(ex, EXPR_TYPE_DISCARD) > 0)
			yyerror("discard cannot be combined with another "
			    "action");
		if (expr_count(ex, EXPR_TYPE_REJECT) > 0)
			yyerror("reject cannot be combined with another "
			    "action");
	}

	npass = expr_count(ex, EXPR_TYPE_PASS);
	if (npass > 0) {
		/* Pass can only be used in conjuction with flag and label. */
		nflag = expr_count(ex, EXPR_TYPE_FLAG);
		nlabel = expr_count(ex, EXPR_TYPE_LABEL);
		if (nactions - nflag - nlabel - 1 > 0)
			yyerror("pass cannot be combined with another action");
	}

	yypopl();
}

static void
expr_validate_attachment_block(const struct expr *ex)
{
	if (expr_count_actions(ex) > expr_count(ex, EXPR_TYPE_EXEC))
		yyerror("attachment cannot be combined with action(s)");
}

static int
yygetc(void)
{
	int c;

	c = fgetc(yyfh);
	if (c == '\n')
		lineno++;
	return c;
}

static int
yypeek(int want)
{
	int c, cc;

	c = yygetc();
	if (c == want)
		return 1;
	if (c == '\\') {
		cc = yygetc();
		yyungetc(cc);
		if (cc == want)
			return 0; /* escaped want */
	}
	yyungetc(c);
	return 0;
}

static void
yyungetc(int c)
{

	if (c == '\n')
		lineno--;
	ungetc(c, yyfh);
}

/*
 * Recover after encountering invalid grammar by discarding the current line.
 */
static void
yyrecover(void)
{
	int c;

	for (;;) {
		c = yygetc();
		if (c == '\n' || c == EOF)
			break;
	}
}

/*
 * Use the given line number in subsequent calls to yyerror(). Must be followed
 * by a call to yypopl() in order to restore the previous line number.
 */
static void
yypushl(int lno)
{

	assert(lineno_save == -1);
	lineno_save = yylval.lineno;
	yylval.lineno = lno;
}

/*
 * Restore previous line number.
 */
static void
yypopl(void)
{

	yylval.lineno = lineno_save;
	lineno_save = -1;
}

static void
macros_free(struct macro_list *macros)
{
	struct macro *mc;

	if (macros == NULL)
		return;

	while ((mc = TAILQ_FIRST(&macros->ml_list)) != NULL) {
		TAILQ_REMOVE(&macros->ml_list, mc, mc_entry);
		if ((mc->mc_flags & MACRO_FLAG_CONST) == 0) {
			free(mc->mc_name);
			free(mc->mc_value);
		}
		if ((mc->mc_flags & MACRO_FLAG_STATIC) == 0)
			free(mc);
	}
	free(macros);
}

static void
macros_validate(const struct macro_list *macros)
{
	const struct macro *mc;

	TAILQ_FOREACH(mc, &macros->ml_list, mc_entry) {
		if (mc->mc_refs > 0)
			continue;

		yypushl(mc->mc_lno);
		yyerror("unused macro: %s", mc->mc_name);
		yypopl();
	}
}

static char *
expand(char *str, unsigned int curctx)
{
	str = expandtilde(str, yyenv->ev_home);
	str = expandmacros(str, yyconfig.cf_macros, curctx);
	return str;
}

static char *
expandmacros(char *str, struct macro_list *macros, unsigned int curctx)
{
	char *buf = NULL;
	size_t i = 0;
	size_t buflen = 0;
	size_t bufsiz = 0;

	while (str[i] != '\0') {
		struct macro *mc;
		char *macro;
		ssize_t n;

		n = ismacro(&str[i], &macro);
		if (n < 0) {
			yyerror("unterminated macro");
			break;
		}
		if (n > 0) {
			unsigned int ctx;

			ctx = macro_context(macro);
			switch (curctx) {
			case MACRO_CTX_ACTION:
				/*
				 * Delay expansion of a non-default macro in a
				 * non-default context.
				 */
				if ((ctx & curctx) == curctx) {
					free(macro);
					goto fallback;
				}
				/*
				 * If the macro is a default one, expand it now.
				 * Otherwise, it's being used in the wrong
				 * context.
				 */
				if ((ctx & MACRO_CTX_DEFAULT))
					break;
				/* FALLTHROUGH */
			case MACRO_CTX_DEFAULT:
				/*
				 * Presence of a non-default macro in a default
				 * context is invalid.
				 */
				if ((ctx & curctx) == 0) {
					yyerror("macro used in wrong context: "
					    "%s", macro);
					free(macro);
					goto fallback;
				}
				break;
			}

			mc = macros_find(macros, macro);
			if (mc != NULL) {
				mc->mc_refs++;
				append(&buf, &bufsiz, &buflen, mc->mc_value);
			} else {
				yyerror("unknown macro used in string: %s",
				    macro);
			}

			free(macro);
			i += n;
		} else {
fallback:
			appendc(&buf, &bufsiz, &buflen, str[i]);
			i++;
		}
	}

	/*
	 * If the string only contains unknown macros, the buffer will not be
	 * allocated.
	 */
	if (buf == NULL)
		return str;
	free(str);
	return buf;
}

static struct string_list *
expandstrings(struct string_list *strings, unsigned int curctx)
{
	struct string *str;

	TAILQ_FOREACH(str, strings, entry) {
		str->val = expand(str->val, curctx);
	}
	return strings;
}

static char *
expandtilde(char *str, const char *home)
{
	char *buf;
	int siz, n;

	if (*str != '~')
		return str;

	siz = PATH_MAX;
	buf = malloc(siz);
	if (buf == NULL)
		err(1, NULL);
	n = snprintf(buf, siz, "%s%s", home, str + 1);
	if (n < 0 || n >= siz)
		yyerror("path too long");
	free(str);
	return buf;
}
