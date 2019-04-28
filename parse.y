%{

#include "config.h"

#include <ctype.h>
#include <err.h>
#include <stdarg.h>
#include <string.h>

#include "extern.h"

static char *expandtilde(char *, const struct environment *);
static void expr_validate(const struct expr *);
static void yyerror(const char *, ...)
	__attribute__((__format__ (printf, 1, 2)));
static int yygetc(void);
static int yylex(void);
static int yypeek(int);
static void yyungetc(int);

static struct config_list config = TAILQ_HEAD_INITIALIZER(config);
static const struct environment *env;
static FILE *fh;
static const char *confpath;
static int lineno, lineno_save, parse_errors;

%}

%union {
	char *str;
	unsigned int i;
	time_t t;

	enum expr_cmp cmp;

	struct expr *expr;
	struct string_list *strings;

	struct {
		char *str;
		unsigned int flags;
	} pattern;
}

%token ALL
%token ATTACHMENT
%token BODY
%token BREAK
%token DATE
%token DISCARD
%token FLAG
%token HEADER
%token INT
%token LABEL
%token MAILDIR
%token MATCH
%token MOVE
%token NEW
%token OLD
%token PATTERN
%token SCALAR
%token STDIN
%token STRING

%type <str> STRING flag maildir_path
%type <i> INT SCALAR attachment optneg
%type <t> date_age
%type <cmp> date_cmp
%type <expr> expr expr1 expr2 expr3 expractions expraction exprblock exprs
%type <strings> stringblock strings
%type <pattern> PATTERN

%left AND OR
%left NEG

%%

grammar		: /* empty */
		| grammar '\n'
		| grammar maildir '\n'
		;

maildir		: maildir_path exprblock {
			struct config *conf;

			/* Favor more specific error messages. */
			if (parse_errors == 0 && expr_count_actions($2) == 0)
				yyerror("empty match block");

			conf = malloc(sizeof(*conf));
			if (conf == NULL)
				err(1, NULL);
			conf->maildir = $1;
			conf->expr = $2;
			TAILQ_INSERT_TAIL(&config, conf, entry);
		}
		;

maildir_path	: MAILDIR STRING {
			  $$ = expandtilde($2, env);
			  if ($$ == NULL)
				  YYERROR;
		}
		| STDIN {
			const struct config *conf;

			$$ = NULL;
			TAILQ_FOREACH(conf, &config, entry)
				if (conf->maildir == NULL)
					yyerror("stdin already defined");
		}
		;

exprblock	: '{' optnl exprs '}' {
			$$ = expr_alloc(EXPR_TYPE_BLOCK, lineno, $3, NULL);
		}
		;

exprs		: /* empty */ {
			$$ = NULL;
		}
		| exprs expr nl {
			if ($1 == NULL)
				$$ = $2;
			else
				$$ = expr_alloc(EXPR_TYPE_OR, lineno, $1, $2);
		}
		| error nl {
			$$ = NULL;
		}
		;

expr		: MATCH expr1 expr2 {
			if (expr_count_patterns($2, EXPR_PATTERN_FORCE) > 1)
				yyerror("pattern force flag cannot be used "
				    "more than once");

			$$ = expr_alloc(EXPR_TYPE_AND, lineno, $2, $3);
		}
		;

expr1		: expr1 AND expr1 {
			$$ = expr_alloc(EXPR_TYPE_AND, lineno, $1, $3);
		}
		| expr1 OR expr1 {
			$$ = expr_alloc(EXPR_TYPE_OR, lineno, $1, $3);
		}
		| NEG expr1 {
			$$ = expr_alloc(EXPR_TYPE_NEG, lineno, $2, NULL);
		}
		| expr3
		;

expr2		: expractions {
			if ($$ == NULL)
				yyerror("missing action");
		}
		| exprblock {
			$$ = $1;
			if (expr_count_actions($$) == 0)
				yyerror("empty nested match block");
		}
		;

expr3		: attachment BODY PATTERN {
			const char *errstr;
			enum expr_type type = $1 ?
				EXPR_TYPE_ATTACHMENT_BODY : EXPR_TYPE_BODY;

			$$ = expr_alloc(type, lineno, NULL, NULL);
			if (expr_set_pattern($$, $3.str, $3.flags, &errstr))
				yyerror("invalid pattern: %s", errstr);
		}
		| attachment HEADER strings PATTERN {
			const char *errstr;
			enum expr_type type = $1 ?
				EXPR_TYPE_ATTACHMENT_HEADER : EXPR_TYPE_HEADER;

			$$ = expr_alloc(type, lineno, NULL, NULL);
			if (expr_set_pattern($$, $4.str, $4.flags, &errstr))
				yyerror("invalid pattern: %s", errstr);
			expr_set_strings($$, $3);
		}
		| ATTACHMENT {
			$$ = expr_alloc(EXPR_TYPE_ATTACHMENT, lineno, NULL, NULL);
		}
		| DATE date_cmp date_age {
			$$ = expr_alloc(EXPR_TYPE_DATE, lineno, NULL, NULL);
			expr_set_date($$, $2, $3);
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
				expr_validate($$);
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
			path = expandtilde($2, env);
			if (path == NULL)
				YYERROR;
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
			expr_set_strings($$, $2);
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

attachment	: /* empty */ {
			$$ = 0;
		}
		| ATTACHMENT {
			$$ = 1;
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

date_cmp	: '<' {
			$$ = EXPR_CMP_LT;
		}
		| '>' {
			$$ = EXPR_CMP_GT;
		}
		;

date_age	: INT SCALAR {
			if ($1 > UINT_MAX / $2) {
				yyerror("integer too large");
				$$ = 0;
			} else {
				$$ = $1 * $2;
			}
		}
		;

optneg		: /* empty */ {
			$$ = 0;
		}
		| NEG {
			$$ = 1;
		}
		;

optnl		: /* empty */
		| '\n' optnl
		;

nl		: '\n' optnl
		;

%%

/*
 * Parses the configuration located at path and returns a config list on
 * success.
 * Otherwise, NULL is returned.
 */
struct config_list *
config_parse(const char *path, const struct environment *envp)
{
	fh = fopen(path, "r");
	if (fh == NULL) {
		warn("%s", path);
		return NULL;
	}
	confpath = path;
	env = envp;

	lineno = 1;
	yyparse();
	fclose(fh);
	if (parse_errors > 0) {
		config_free(&config);
		return NULL;
	}
	return &config;
}

void
config_free(struct config_list *config)
{
	struct config *conf;

	if (config == NULL)
		return;

	while ((conf = TAILQ_FIRST(config)) != NULL) {
		TAILQ_REMOVE(config, conf, entry);
		expr_free(conf->expr);
		free(conf->maildir);
		free(conf);
	}
}

void
yyerror(const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "%s:%d: ", confpath, lineno_save);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");

	parse_errors++;
}

int
yylex(void)
{
	static struct {
		const char *str;
		int type;
	} keywords[] = {
		{ "all",	ALL },
		{ "and",	AND },
		{ "attachment",	ATTACHMENT },
		{ "body",	BODY },
		{ "break",	BREAK },
		{ "date",	DATE },
		{ "discard",	DISCARD },
		{ "flag",	FLAG },
		{ "header",	HEADER },
		{ "label",	LABEL },
		{ "maildir",	MAILDIR },
		{ "match",	MATCH },
		{ "move",	MOVE },
		{ "new",	NEW },
		{ "old",	OLD },
		{ "or",		OR },
		{ "stdin",	STDIN },

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
	static char lexeme[BUFSIZ], kw[16];
	char *buf;
	size_t len;
	int ambiguous, c, i, match, overflow;

	buf = lexeme;

again:
	for (c = yygetc(); c == ' ' || c == '\t'; c = yygetc())
		continue;
	if (c == '\\' && yypeek('\n'))
		goto again;

	/* Used for more accurate error messages. */
	lineno_save = lineno;

	yylval.i = c;
	if (c == EOF) {
		return 0;
	} else if (c == '!') {
		return NEG;
	} else if (c == '#') {
		for (;;) {
			c = yygetc();
			if (c == '\n') {
				yyungetc(c);
				goto again;
			}
			if (c == EOF)
				return 0;
		}
	} else if (c == '"') {
		for (;;) {
			if (yypeek('"'))
				break;
			c = yygetc();
			if (c == EOF) {
				yyerror("unterminated string");
				return 0;
			}

			if (buf == lexeme + sizeof(lexeme) - 1) {
				yyerror("string too long");
				return 0;
			}
			*buf++ = c;
		}
		*buf = '\0';
		yylval.str = strdup(lexeme);
		if (yylval.str == NULL)
			err(1, NULL);
		if (strlen(yylval.str) == 0)
			yyerror("empty string");
		return STRING;
	} else if (c == '/') {
		for (;;) {
			if (yypeek('/'))
				break;
			c = yygetc();
			if (c == EOF) {
				yyerror("unterminated pattern");
				return 0;
			}

			if (buf == lexeme + sizeof(lexeme) - 1) {
				yyerror("pattern too long");
				return 0;
			}
			*buf++ = c;
		}
		*buf = '\0';
		yylval.pattern.str = lexeme;

		yylval.pattern.flags = 0;
		for (;;) {
			c = yygetc();
			switch (c) {
			case 'f':
				yylval.pattern.flags |= EXPR_PATTERN_FORCE;
				break;
			case 'i':
				yylval.pattern.flags |= EXPR_PATTERN_ICASE;
				break;
			default:
				yyungetc(c);
				return PATTERN;
			}
		}
	} else if (isdigit(c)) {
		yylval.i = overflow = 0;
		for (; isdigit(c); c = yygetc()) {
			if (overflow)
				continue;

			if (yylval.i > UINT_MAX / 10) {
				yyerror("integer too large");
				overflow = 1;
				continue;
			}
			yylval.i *= 10;

			if (yylval.i > UINT_MAX - (c - '0')) {
				yyerror("integer too large");
				overflow = 1;
				continue;
			}
			yylval.i += c - '0';
		}
		yyungetc(c);
		return INT;
	} else if (islower(c)) {
		buf = kw;
		for (; islower(c); c = yygetc()) {
			if (buf == kw + sizeof(kw) - 1) {
				yyerror("keyword too long");
				return 0;
			}
			*buf++ = c;
		}
		*buf = '\0';
		yyungetc(c);

		for (i = 0; keywords[i].str != NULL; i++)
			if (strcmp(kw, keywords[i].str) == 0)
				return keywords[i].type;

		len = strlen(kw);
		ambiguous = 0;
		match = -1;
		for (i = 0; scalars[i].str != NULL; i++) {
			if (strncmp(kw, scalars[i].str, len) == 0) {
				if (match >= 0)
					ambiguous++;
				match = i;
			}
		}
		if (ambiguous) {
			yyerror("ambiguous keyword: %s", kw);
		} else if (match >= 0) {
			yylval.i = scalars[match].val;
			return SCALAR;
		} else {
			yyerror("unknown keyword: %s", kw);
		}
	}

	return c;
}

static char *
expandtilde(char *str, const struct environment *env)
{
	char *buf;
	int len, n;

	if (*str != '~')
		return str;

	len = PATH_MAX;
	buf = malloc(len);
	if (buf == NULL)
		err(1, NULL);
	n = snprintf(buf, len, "%s%s", env->ev_home, str + 1);
	if (n < 0 || n >= len)
		yyerror("path too long");
	free(str);
	return buf;
}

static void
expr_validate(const struct expr *ex)
{
	int nactions;

	if (expr_count(ex, EXPR_TYPE_MOVE) > 1)
		yyerror("move action already defined");
	if (expr_count(ex, EXPR_TYPE_LABEL) > 1)
		yyerror("label action already defined");

	nactions = expr_count_actions(ex);
	if (nactions <= 1)
		return;
	if (expr_count(ex, EXPR_TYPE_BREAK) > 0)
		yyerror("break cannot be combined with another action");
	if (expr_count(ex, EXPR_TYPE_DISCARD) > 0)
		yyerror("discard cannot be combined with another action");
}

static int
yygetc(void)
{
	int c;

	c = fgetc(fh);
	if (c == '\n')
		lineno++;
	return c;
}

static int
yypeek(int want)
{
	int c;

	c = yygetc();
	if (c == '\\') {
		c = yygetc();
		yyungetc(c);
		if (c == want)
			return 0;
		c = '\\';
	} else if (c == want) {
		return 1;
	}
	yyungetc(c);
	return 0;
}

static void
yyungetc(int c)
{
	if (c == '\n')
		lineno--;
	ungetc(c, fh);
}
