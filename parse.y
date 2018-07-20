%{

#include "config.h"

#include <ctype.h>
#include <err.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "extern.h"

static char *expandtilde(char *);
static void yyerror(const char *, ...)
	__attribute__((__format__ (printf, 1, 2)));
static int yygetc(void);
static int yylex(void);
static int yypeek(int);
static void yyungetc(int);

static struct config_list config = TAILQ_HEAD_INITIALIZER(config);
static FILE *fh;
static const char *confpath;
static int lineno, lineno_save, parse_errors;

%}

%union {
	char *str;

	struct expr *expr;
	struct expr_headers *headers;

	struct {
		char *str;
		int flags;
	} pattern;
}

%token BODY HEADER MAILDIR MATCH MOVE NEW PATTERN STRING
%type <str> STRING move
%type <expr> expr expr1 expr2 expr3 exprblock exprs
%type <headers> headers strings
%type <pattern> PATTERN

%left AND OR
%left NEG

%%

grammar		: /* empty */
		| grammar '\n'
		| grammar maildir '\n'
		;

maildir		: MAILDIR STRING exprblock {
			struct config *conf;
			char *path;

			path = expandtilde($2);
			if (path == NULL)
				YYERROR;

			conf = malloc(sizeof(*conf));
			if (conf == NULL)
				err(1, NULL);
			conf->maildir = path;
			conf->rule = rule_alloc($3);
			TAILQ_INSERT_TAIL(&config, conf, entry);
		}
		;

exprblock	: '{' optnl exprs '}' {
			$$ = $3;
		}
		;

exprs		: /* empty */ {
			$$ = NULL;
		}
		| exprs expr nl {
			if ($1 == NULL)
				$$ = $2;
			else
				$$ = expr_alloc(EXPR_TYPE_OR, $1, $2);
		}
		| error nl {
			$$ = NULL;
		}
		;

expr		: MATCH expr1 expr2 {
			$$ = expr_alloc(EXPR_TYPE_AND, $2, $3);
		}
		;

expr1		: expr1 AND expr1 {
			$$ = expr_alloc(EXPR_TYPE_AND, $1, $3);
		}
		| expr1 OR expr1 {
			$$ = expr_alloc(EXPR_TYPE_OR, $1, $3);
		}
		| NEG expr1 {
			$$ = expr_alloc(EXPR_TYPE_NEG, $2, NULL);
		}
		| expr3
		;

expr2		: move {
			$$ = expr_alloc(EXPR_TYPE_MOVE, NULL, NULL);
			expr_set_dest($$, $1);
		}
		| exprblock {
			$$ = $1;
			if ($$ == NULL) {
				yyerror("empty nested match block");
				/* Abort, avoids handling NULL upwards. */
				YYERROR;
			}
		}
		;

expr3		: BODY PATTERN {
			const char *errstr;

			$$ = expr_alloc(EXPR_TYPE_BODY, NULL, NULL);
			if (expr_set_pattern($$, $2.str, $2.flags, &errstr))
				yyerror("invalid pattern: %s", errstr);
		}
		| HEADER headers PATTERN {
			const char *errstr;

			$$ = expr_alloc(EXPR_TYPE_HEADER, NULL, NULL);
			if (expr_set_pattern($$, $3.str, $3.flags, &errstr))
				yyerror("invalid pattern: %s", errstr);
			expr_set_headers($$, $2);
		}
		| NEW {
			$$ = expr_alloc(EXPR_TYPE_NEW, NULL, NULL);
		}
		| '(' expr1 ')' {
			$$ = $2;
		}
		;

headers		: '{' strings '}' {
			$$ = $2;
		}
		| STRING {
			$$ = expr_headers_alloc();
			expr_headers_append($$, $1);
		}
		;

strings		: /* empty */ {
			$$ = expr_headers_alloc();
		}
		| strings STRING {
			$$ = $1;
			expr_headers_append($$, $2);
		}
		;

move		: MOVE STRING {
			char *path;

			path = expandtilde($2);
			if (path == NULL)
				YYERROR;
			$$ = path;
		}
		;

optnl		: /* empty */
		| '\n' optnl
		;

nl		: '\n' optnl
		;

%%

struct config_list *
parse_config(const char *path)
{
	fh = fopen(path, "r");
	if (fh == NULL) {
		warn("%s", path);
		return NULL;
	}
	confpath = path;

	lineno = 1;
	yyparse();
	fclose(fh);
	if (parse_errors > 0)
		return NULL;
	return &config;
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
		{ "and",	AND },
		{ "body",	BODY },
		{ "header",	HEADER },
		{ "maildir",	MAILDIR },
		{ "match",	MATCH },
		{ "move",	MOVE },
		{ "new",	NEW },
		{ "or",		OR },
		{ NULL,		0 },
	};
	static char lexeme[BUFSIZ], kw[16];
	char *buf;
	int c, flag, i;

	buf = lexeme;

again:
	for (c = yygetc(); c == ' ' || c == '\t'; c = yygetc())
		continue;
	if (c == '\\' && yypeek('\n'))
		goto again;

	/* Used for more accurate error messages. */
	lineno_save = lineno;

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
			if (c == 'i') {
				flag = EXPR_PATTERN_ICASE;
			} else {
				yyungetc(c);
				break;
			}
			if (yylval.pattern.flags & flag)
				yyerror("duplicate pattern flag: %c", c);
			yylval.pattern.flags |= flag;
		}

		return PATTERN;
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
		yyerror("unknown keyword: %s", kw);
	}

	return c;
}

static char *
expandtilde(char *str)
{
	size_t hlen, slen;

	if (*str != '~')
		return str;

	hlen = strlen(home);
	slen = strlen(str);
	if (hlen + slen - 1 >= PATH_MAX - 1) {
		yyerror("path too long");
		return NULL;
	}
	str = realloc(str, hlen + slen + 1);
	if (str == NULL)
		err(1, NULL);
	/*
	 * Do not copy leading tilde, slen will therefore guarantee
	 * NUL-termination.
	 */
	memmove(str + hlen, str + 1, slen);
	memcpy(str, home, hlen);
	return str;
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
