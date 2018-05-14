%{

#include "config.h"

#include <ctype.h>
#include <err.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "extern.h"

static int expandtilde(char *);
static void yyerror(const char *, ...)
	__attribute__((__format__ (printf, 1, 2)));
static int yygetc(void);
static int yylex(void);
static int yypeek(int);
static void yyungetc(int);

static struct config_list config = TAILQ_HEAD_INITIALIZER(config);
static struct config *curconf;
static FILE *fh;
static const char *confpath;
static int flags, lineno, lineno_save, newline, parse_errors;

%}

%union {
	int i;
	char *s;
	struct rule *r;
	struct expr *e;
	struct expr_headers *h;
	struct {
		char *s;
		int i;
	} p;
}

%token BODY HEADER MAILDIR MATCH MOVE NEW PATTERN STRING
%type <i> flags flag
%type <s> PATTERN STRING action
%type <r> rule
%type <e> exprs expr
%type <h> headers strings
%type <p> pattern

%left AND OR
%left NEG

%%

grammar		: /* empty */
		| grammar maildir
		;

maildir		: MAILDIR STRING {
			struct config *conf;
			char *maildir;

			if (expandtilde($2))
				YYERROR;
			maildir = strdup($2);
			if (maildir == NULL)
				err(1, NULL);

			conf = malloc(sizeof(*conf));
			if (conf == NULL)
				err(1, NULL);
			conf->maildir = maildir;
			TAILQ_INIT(&conf->rules);
			TAILQ_INSERT_TAIL(&config, conf, entry);

			curconf = conf;
		} '{' rules '}'
		;

rules		: /* empty */
		| rules {
			newline = 1;
		} rule '\n' {
			TAILQ_INSERT_TAIL(&curconf->rules, $3, entry);
		}
		| error '\n'
		;

rule		: MATCH exprs action {
			$$ = rule_alloc($2, $3);
		}
		;

exprs		: exprs AND exprs {
			$$ = expr_alloc(EXPR_TYPE_AND, $1, $3);
		}
		| exprs OR exprs {
			$$ = expr_alloc(EXPR_TYPE_OR, $1, $3);
		}
		| NEG exprs {
			$$ = expr_alloc(EXPR_TYPE_NEG, $2, NULL);
		}
		| expr
		;

expr		: BODY pattern {
			const char *errstr;

			$$ = expr_alloc(EXPR_TYPE_BODY, NULL, NULL);
			if (expr_set_pattern($$, $2.s, $2.i, &errstr))
				yyerror("invalid pattern: %s", errstr);
		}
		| HEADER headers pattern {
			const char *errstr;

			$$ = expr_alloc(EXPR_TYPE_HEADER, NULL, NULL);
			expr_set_headers($$, $2);
			if (expr_set_pattern($$, $3.s, $3.i, &errstr))
				yyerror("invalid pattern: %s", errstr);
		}
		| NEW {
			$$ = expr_alloc(EXPR_TYPE_NEW, NULL, NULL);
		}
		| '(' exprs ')' {
			$$ = $2;
		}
		;

headers		: '{' strings '}' {
			$$ = $2;
		}
		| STRING {
			if (strlen($1) == 0)
				yyerror("missing header name");
			$$ = expr_headers_alloc();
			expr_headers_append($$, $1);
		}
		;

strings		: /* empty */ {
			$$ = expr_headers_alloc();
		}
		| strings STRING {
			if (strlen($2) == 0)
				yyerror("missing header name");

			$$ = $1;
			expr_headers_append($$, $2);
		}
		;

pattern		: PATTERN flags {
			flags = 0;
			$$.s = $1;
			$$.i = $2;
		}
		;

flags		: /* empty */ {
			flags = 1;
			$$ = 0;
		}
		| flags flag {
			$$ = $1 | $2;
		}
		;

flag		: 'i' {
			$$ = EXPR_PATTERN_ICASE;
		}
		;

action		: MOVE STRING {
			if (expandtilde($2))
				YYERROR;
			$$ = $2;
		}
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
	int c, i;

	buf = yylval.s = lexeme;

again:
	for (c = yygetc(); c == ' ' || c == '\t'; c = yygetc())
		continue;
	if (c == '\n') {
		/* Munge newlines. */
		for (; c == '\n'; c = yygetc())
			continue;
		yyungetc(c);
		if (newline) {
			newline = 0;
			return '\n';
		}
		goto again;
	} else if (c == '\\' && newline) {
		/* Allow line continuation if newline is allowed. */
		c = yygetc();
		if (c == '\n')
			goto again;
		yyungetc(c);
	}
	/* Used for more accurate error messages. */
	lineno_save = lineno;

	if (c == '#') {
		for (;;) {
			c = yygetc();
			if (c == '\n') {
				yyungetc(c);
				goto again;
			}
			if (c == EOF)
				return 0;
		}
	}

	switch (c) {
	case EOF:
		return 0;
	case '!':
		return NEG;
	case '(':
	case ')':
	case '{':
	case '}':
		return c;
	case 'i':
		if (flags)
			return c;
		break;
	}

	if (c == '"') {
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
		return STRING;
	}

	if (c == '/') {
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
		return PATTERN;
	}

	if (islower(c)) {
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

static int
expandtilde(char *str)
{
	size_t hlen, slen;

	if (*str != '~')
		return 0;

	hlen = strlen(home);
	slen = strlen(str);
	if (hlen + slen - 1 >= PATH_MAX - 1) {
		yyerror("path too long");
		return 1;
	}
	/*
	 * Do not copy leading tilde, slen will therefore guarantee
	 * NUL-termination.
	 */
	memmove(str + hlen, str + 1, slen);
	memcpy(str, home, hlen);
	return 0;
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
