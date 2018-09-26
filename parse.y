%{

#include <ctype.h>
#include <err.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "extern.h"

static char *expandtilde(char *);
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
	int i;

	struct expr *expr;
	struct string_list *strings;

	struct {
		char *str;
		int flags;
	} pattern;
}

%token ALL BODY FLAG HEADER MAILDIR MATCH MOVE NEW OLD PATTERN STRING
%type <str> STRING flag
%type <i> optneg
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

maildir		: MAILDIR STRING exprblock {
			struct config *conf;
			char *path;

			path = expandtilde($2);
			if (path == NULL)
				YYERROR;

			if ($3 == NULL && parse_errors == 0)
				yyerror("empty match block");

			conf = malloc(sizeof(*conf));
			if (conf == NULL)
				err(1, NULL);
			conf->maildir = path;
			conf->expr = expr_alloc(EXPR_TYPE_ROOT, $3, NULL);
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

expr2		: expractions {
			if ($$ == NULL)
				yyerror("missing action");
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
		| HEADER strings PATTERN {
			const char *errstr;

			$$ = expr_alloc(EXPR_TYPE_HEADER, NULL, NULL);
			if (expr_set_pattern($$, $3.str, $3.flags, &errstr))
				yyerror("invalid pattern: %s", errstr);
			expr_set_strings($$, $2);
		}
		| NEW {
			$$ = expr_alloc(EXPR_TYPE_NEW, NULL, NULL);
		}
		| OLD {
			$$ = expr_alloc(EXPR_TYPE_OLD, NULL, NULL);
		}
		| ALL {
			$$ = expr_alloc(EXPR_TYPE_ALL, NULL, NULL);
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
				$$ = expr_alloc(EXPR_TYPE_AND, $1, $2);
				if (expr_count($$, EXPR_TYPE_MOVE) > 1)
					yyerror("move action already defined");
			}
		}
		;

expraction	: MOVE STRING {
			struct string_list *strings;
			char *path;

			$$ = expr_alloc(EXPR_TYPE_MOVE, NULL, NULL);
			path = expandtilde($2);
			if (path == NULL)
				YYERROR;
			strings = strings_alloc();
			strings_append(strings, path);
			expr_set_strings($$, strings);
		}
		| FLAG flag {
			struct string_list *strings;

			$$ = expr_alloc(EXPR_TYPE_FLAG, NULL, NULL);
			strings = strings_alloc();
			strings_append(strings, $2);
			expr_set_strings($$, strings);
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

struct config_list *
parse_config(const char *path, const struct environment *envp)
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
		{ "all",	ALL },
		{ "and",	AND },
		{ "body",	BODY },
		{ "flag",	FLAG },
		{ "header",	HEADER },
		{ "maildir",	MAILDIR },
		{ "match",	MATCH },
		{ "move",	MOVE },
		{ "new",	NEW },
		{ "old",	OLD },
		{ "or",		OR },
		{ NULL,		0 },
	};
	static char lexeme[BUFSIZ], kw[16];
	char *buf;
	int c, i;

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
		c = yygetc();
		if (c == 'i')
			yylval.pattern.flags = EXPR_PATTERN_ICASE;
		else
			yyungetc(c);

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
	char *buf;
	int len, n;

	if (*str != '~')
		return str;

	len = PATH_MAX;
	buf = malloc(len);
	if (buf == NULL)
		err(1, NULL);
	n = snprintf(buf, len, "%s%s", env->home, str + 1);
	if (n == -1 || n >= len)
		yyerror("path too long");
	free(str);
	return buf;
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
