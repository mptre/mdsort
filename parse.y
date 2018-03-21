%{

#include "config.h"

#include <ctype.h>
#include <err.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "extern.h"

static int expandtilde(char *);
static void yyerror(const char *);
static int yygetc(void);
static int yylex(void);
static int yypeek(int);
static void yyungetc(int);

static struct config_list config;
static struct config *curconf;
static FILE *fh;
static const char *confpath;
static int flags, lineno, newline, parse_errors;

%}

%union {
	int i;
	char *s;
	struct rule *r;
	struct expr *e;
}

%token AND BODY HEADER MAILDIR MATCH MOVE NEW OR PATTERN STRING
%type <i> AND OR binop flags flag neg
%type <s> PATTERN STRING action
%type <e> expr expr1
%type <r> rule

%%

grammar		: /* empty */
		| grammar maildir
		;

maildir		: MAILDIR STRING {
			char *maildir;

			config.list = reallocarray(config.list,
			    config.nmemb + 1, sizeof(*config.list));
			if (config.list == NULL)
				err(1, NULL);
			curconf = config.list + config.nmemb;
			config.nmemb++;
			if (expandtilde($2))
				YYERROR;
			maildir = strdup($2);
			if (maildir == NULL)
				err(1, NULL);
			curconf->maildir = maildir;
			curconf->rules = NULL;
			curconf->nrules = 0;
		} '{' rules '}'
		;

rules		: /* empty */
		| rules {
			newline = 1;
		} rule '\n' {
			curconf->rules = reallocarray(curconf->rules,
			    curconf->nrules + 1, sizeof(*curconf->rules));
			if (curconf->rules == NULL)
				err(1, NULL);
			curconf->rules[curconf->nrules] = $3;
			curconf->nrules++;
		}
		| error '\n'
		;

rule		: MATCH {
			$<r>$ = rule_alloc();
		} exprs action {
			rule_set_dest($<r>2, $4);
			$$ = $<r>2;
		}
		;

exprs		: /* empty */
		| exprs binop expr {
			if (rule_set_type($<r>0, $2))
				yyerror("and/or are disjoint");
			rule_add_expr($<r>0, $3);
		}
		| expr {
			rule_add_expr($<r>0, $1);
		}
		;

binop		: AND {
			$$ = RULE_TYPE_AND;
		}
		| OR {
			$$ = RULE_TYPE_OR;
		}
		;

expr		: neg expr1 {
			expr_set_negate($2, $1);
			$$ = $2;
		}
		;

expr1		: BODY PATTERN flags {
			const char *errstr;

			$$ = expr_alloc(EXPR_TYPE_BODY);
			if (expr_set_pattern($$, $2, $3, &errstr))
				yyerror(errstr);
			flags = 0;
		}
		| HEADER {
			$<e>$ = expr_alloc(EXPR_TYPE_HEADER);
		} headers PATTERN flags {
			const char *errstr;

			if (expr_set_pattern($<e>2, $4, $5, &errstr))
				yyerror(errstr);
			flags = 0;
			$$ = $<e>2;
		}
		| NEW {
			$$ = expr_alloc(EXPR_TYPE_NEW);
		}
		;

neg		: /* empty */ {
			$$ = 0;
		}
		| '!' {
			$$ = 1;
		}
		;

headers		: '{' {
			$<e>$ = $<e>0;
		} strings '}'
		| STRING {
			if (strlen($1) == 0)
				yyerror("missing header name");
			expr_set_header_key($<e>0, $1);
		}
		;

strings		: /* empty */
		| strings STRING {
			if (strlen($2) == 0)
				yyerror("missing header name");
			expr_set_header_key($<e>0, $2);
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

const struct config_list *
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
yyerror(const char *fmt)
{
	fprintf(stderr, "%s:%d: %s\n", confpath, lineno, fmt);
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
	int c, i, lineno_save;

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
				lineno = lineno_save;
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
				lineno = lineno_save;
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
