%{

#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "libks/arithmetic.h"
#include "libks/buffer.h"
#include "libks/vector.h"
#include "libks/compiler.h"

#include "conf.h"
#include "environment.h"
#include "expr.h"
#include "macro.h"
#include "string-list.h"
#include "util.h"

static void expr_validate(const struct expr *);
static void expr_validate_attachment_block(const struct expr *);
static void yyerror(const char *, ...)
	__attribute__((__format__ (printf, 1, 2)));
static void yyerror_at_line(unsigned int, const char *, ...)
	__attribute__((__format__ (printf, 2, 3)));
static void yyverror(const char *, va_list, unsigned int);
static int yygetc(void);
static int yylex(void);
static int yylex1(int);
static int yypeek(int);
static void yyungetc(int);
static void yyrecover(void);

static void macros_validate(const struct macro_list *);

static char *expand(char *, unsigned int);
static char *expandmacros(char *, struct macro_list *, unsigned int);
static struct string_list *expandstrings(struct string_list *, unsigned int);
static char *expandtilde(char *, const char *);

/* yacc parser global state */
static struct {
	const char			*path;
	const struct environment	*env;
	struct config_list		*config;
	FILE				*fh;
	struct arena_scope		*scope;
	unsigned int			 lineno;
	int				 error;
	int				 sflag;
	int				 pflag;
} parser_state;

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
	};
	unsigned int lineno;
} YYSTYPE;

%}

%token ACCESS
%token ADDHEADER
%token ALL
%token ATTACHMENT
%token BODY
%token BREAK
%token COMMAND
%token CREATED
%token DATE
%token DISCARD
%token EXEC
%token FLAG
%token FLAGS
%token HEADER
%token ISDIRECTORY
%token LABEL
%token MAILDIR
%token MATCH
%token MODIFIED
%token MOVE
%token NEW
%token OLD
%token PASS
%token REJECT
%token STDIN
%token SYNC

%type	<cmp>		date_cmp
%type	<expr>		expr
%type	<expr>		expr1
%type	<expr>		expr2
%type	<expr>		expr3
%type	<expr>		expraction
%type	<expr>		expractions
%type	<expr>		exprblock
%type	<expr>		exprs
%type	<field>		date_field
%token	<number>	INT
%token	<number>	SCALAR
%type	<number>	exec_flag
%type	<number>	exec_flags
%type	<number>	optneg
%type	<number>	scalar
%token	<pattern>	PATTERN
%type	<pattern>	pattern
%token	<string>	MACRO
%token	<string>	STRING
%type	<string>	flag
%type	<strings>	maildir_paths
%type	<strings>	stringblock
%type	<strings>	strings
%type	<time>		date_age

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
			struct macro_list *macros = parser_state.config->cl_macros;

			$3 = expand($3, MACRO_CTX_DEFAULT);
			switch (macros_insert(macros, $1, $3, 0, parser_state.lineno)) {
			case MACRO_ERR_NONE:
				break;
			case MACRO_ERR_CTX:
			case MACRO_ERR_EXIST:
				yyerror("macro already defined: %s", $1);
				FALLTHROUGH;
			case MACRO_ERR_STICKY:
				free($1);
				free($3);
				break;
			}
		}
		;

maildir		: maildir_paths exprblock {
			struct config *conf;
			struct string *str;

			/* Favor more specific error messages. */
			if (parser_state.error == 0 &&
			    expr_count_actions($2) == 0)
				yyerror("empty match block");

			TAILQ_FOREACH(str, $1, entry) {
				if (isstdin(str->val) ||
				    expr_count($2, EXPR_TYPE_REJECT) == 0)
					continue;

				/*
				 * All maildir paths share the same expression,
				 * therefore only report the error once.
				 */
				yyerror("reject cannot be used outside stdin");
				break;
			}

			conf = config_list_append(parser_state.config);
			conf->paths = $1;
			conf->expr = $2;
		}
		;

maildir_paths	: MAILDIR strings {
			struct string *str;

			$$ = $2;
			TAILQ_FOREACH(str, $$, entry) {
				str->val = expand(str->val, MACRO_CTX_DEFAULT);
			}
		}
		| STDIN {
			char *path;
			size_t i;

			for (i = 0; i < VECTOR_LENGTH(parser_state.config->cl_list); i++) {
				const struct config *conf = &parser_state.config->cl_list[i];
				const struct string *str;

				TAILQ_FOREACH(str, conf->paths, entry) {
					if (isstdin(str->val))
						yyerror("stdin already defined");
				}
			}

			$$ = strings_alloc();
			path = strdup("/dev/stdin");
			if (path == NULL)
				err(1, NULL);
			strings_append($$, path);
		}
		;

exprblock	: '{' exprs '}' {
			$$ = expr_alloc(EXPR_TYPE_BLOCK, parser_state.lineno,
			    $2, NULL, parser_state.scope);
		}
		;

exprs		: /* empty */ {
			$$ = NULL;
		}
		| exprs expr {
			if ($1 == NULL) {
				$$ = $2;
			} else {
				$$ = expr_alloc(EXPR_TYPE_OR,
				    parser_state.lineno, $1, $2,
				    parser_state.scope);
			}
		}
		| error {
			yyrecover();
			$$ = NULL;
		}
		;

expr		: MATCH expr1 expr2 {
			$$ = expr_alloc(EXPR_TYPE_MATCH, parser_state.lineno,
			    $2, $3, parser_state.scope);
		}
		;

expr1		: expr1 AND expr1 {
			$$ = expr_alloc(EXPR_TYPE_AND, parser_state.lineno,
			    $1, $3, parser_state.scope);
		}
		| expr1 OR expr1 {
			$$ = expr_alloc(EXPR_TYPE_OR, parser_state.lineno,
			    $1, $3, parser_state.scope);
		}
		| ATTACHMENT expr1 {
			$$ = expr_alloc(EXPR_TYPE_ATTACHMENT,
			    parser_state.lineno, $2, NULL, parser_state.scope);
		}
		| NEG expr1 {
			$$ = expr_alloc(EXPR_TYPE_NEG, parser_state.lineno,
			    $2, NULL, parser_state.scope);
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

			$$ = expr_alloc(EXPR_TYPE_BODY, parser_state.lineno,
			    NULL, NULL, parser_state.scope);
			if (expr_set_pattern($$, $2.string, $2.flags, &errstr,
			    parser_state.scope))
				yyerror("invalid pattern: %s", errstr);
		}
		| HEADER strings pattern {
			const char *errstr;

			$$ = expr_alloc(EXPR_TYPE_HEADER, parser_state.lineno,
			    NULL, NULL, parser_state.scope);
			if (expr_set_pattern($$, $3.string, $3.flags, &errstr,
			    parser_state.scope))
				yyerror("invalid pattern: %s", errstr);
			$2 = expandstrings($2, MACRO_CTX_DEFAULT);
			expr_set_strings($$, $2);
		}
		| DATE date_field date_cmp date_age {
			$$ = expr_alloc(EXPR_TYPE_DATE, parser_state.lineno,
			    NULL, NULL, parser_state.scope);
			expr_set_date($$, $2, $3, $4, parser_state.scope);
		}
		| NEW {
			$$ = expr_alloc(EXPR_TYPE_NEW, parser_state.lineno,
			    NULL, NULL, parser_state.scope);
		}
		| OLD {
			$$ = expr_alloc(EXPR_TYPE_OLD, parser_state.lineno,
			    NULL, NULL, parser_state.scope);
		}
		| ALL {
			$$ = expr_alloc(EXPR_TYPE_ALL, parser_state.lineno,
			    NULL, NULL, parser_state.scope);
		}
		| ISDIRECTORY STRING {
			char *path;

			$$ = expr_alloc(EXPR_TYPE_STAT, parser_state.lineno,
			    NULL, NULL, parser_state.scope);
			path = expand($2, MACRO_CTX_DEFAULT);
			expr_set_stat($$, path, EXPR_STAT_DIR);
		}
		| COMMAND strings {
			$$ = expr_alloc(EXPR_TYPE_COMMAND, parser_state.lineno,
			    NULL, NULL, parser_state.scope);
			$2 = expandstrings($2, MACRO_CTX_DEFAULT);
			if (expr_set_exec($$, $2, 0))
				yyerror("invalid command options");
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
				$$ = expr_alloc(EXPR_TYPE_AND,
				    parser_state.lineno, $1, $2,
				    parser_state.scope);
			}
		}
		;

expraction	: BREAK {
			$$ = expr_alloc(EXPR_TYPE_BREAK, parser_state.lineno,
			    NULL, NULL, parser_state.scope);
		}
		| MOVE STRING {
			struct string_list *strings;
			char *path;

			$$ = expr_alloc(EXPR_TYPE_MOVE, parser_state.lineno,
			    NULL, NULL, parser_state.scope);
			path = expand($2, MACRO_CTX_ACTION);
			strings = strings_alloc();
			strings_append(strings, path);
			expr_set_strings($$, strings);
		}
		| FLAG flag {
			struct string_list *strings;

			$$ = expr_alloc(EXPR_TYPE_FLAG, parser_state.lineno,
			    NULL, NULL, parser_state.scope);
			strings = strings_alloc();
			strings_append(strings, $2);
			expr_set_strings($$, strings);
		}
		| FLAGS STRING {
			struct string_list *strings;

			$$ = expr_alloc(EXPR_TYPE_FLAGS, parser_state.lineno,
			    NULL, NULL, parser_state.scope);
			strings = strings_alloc();
			strings_append(strings, $2);
			expr_set_strings($$, strings);
		}
		| DISCARD {
			$$ = expr_alloc(EXPR_TYPE_DISCARD, parser_state.lineno,
			    NULL, NULL, parser_state.scope);
		}
		| LABEL strings {
			$$ = expr_alloc(EXPR_TYPE_LABEL, parser_state.lineno,
			    NULL, NULL, parser_state.scope);
			$2 = expandstrings($2, MACRO_CTX_ACTION);
			expr_set_strings($$, $2);
		}
		| PASS {
			$$ = expr_alloc(EXPR_TYPE_PASS, parser_state.lineno,
			    NULL, NULL, parser_state.scope);
		}
		| REJECT {
			$$ = expr_alloc(EXPR_TYPE_REJECT, parser_state.lineno,
			    NULL, NULL, parser_state.scope);
		}
		| EXEC exec_flags strings {
			$$ = expr_alloc(EXPR_TYPE_EXEC, parser_state.lineno,
			    NULL, NULL, parser_state.scope);
			$3 = expandstrings($3, MACRO_CTX_ACTION);
			if (expr_set_exec($$, $3, $2))
				yyerror("invalid exec options");
		}
		| ATTACHMENT exprblock {
			expr_validate_attachment_block($2);
			$$ = expr_alloc(EXPR_TYPE_ATTACHMENT_BLOCK,
			    parser_state.lineno, $2, NULL, parser_state.scope);
		}
		| ADDHEADER STRING STRING {
			$$ = expr_alloc(EXPR_TYPE_ADD_HEADER,
			    parser_state.lineno, NULL, NULL, parser_state.scope);
			expr_set_add_header($$, $2, $3);
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
			unsigned int age;

			if (KS_u32_mul_overflow($1, $2, &age)) {
				yyerror("integer too large");
				$$ = 0;
			} else {
				$$ = (time_t)age;
			}
		}
		;

scalar		: /* backdoor */ {
			parser_state.sflag = 1;
		} SCALAR {
			parser_state.sflag = 0;
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
			parser_state.pflag = 1;
		} PATTERN {
			parser_state.pflag = 0;
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

int
config_list_parse(struct config_list *cl, const char *path,
    const struct environment *env, struct arena_scope *s)
{
	memset(&parser_state, 0, sizeof(parser_state));
	parser_state.path = path;
	parser_state.env = env;
	parser_state.config = cl;
	parser_state.scope = s;
	parser_state.lineno = 1;

	parser_state.fh = fopen(path, "r");
	if (parser_state.fh == NULL) {
		warn("%s", path);
		return 1;
	}

	yyparse();
	fclose(parser_state.fh);
	macros_validate(parser_state.config->cl_macros);
	return parser_state.error;
}

static void
yyerror(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	yyverror(fmt, ap, yylval.lineno);
	va_end(ap);
}

static void
yyerror_at_line(unsigned int lineno, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	yyverror(fmt, ap, lineno);
	va_end(ap);
}

static void
yyverror(const char *fmt, va_list ap, unsigned lineno)
{
	fprintf(stderr, "%s:%u: ", parser_state.path, lineno);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	parser_state.error++;
}

static int
yylex(void)
{
	static int last_token;

	last_token = yylex1(last_token);
	return last_token;
}

static int
yylex1(int last_token)
{
	static struct {
		const char *str;
		int type;
	} keywords[] = {
		{ "access",		ACCESS },
		{ "add-header",		ADDHEADER },
		{ "all",		ALL },
		{ "and",		AND },
		{ "attachment",		ATTACHMENT },
		{ "body",		BODY },
		{ "break",		BREAK },
		{ "command",		COMMAND },
		{ "created",		CREATED },
		{ "date",		DATE },
		{ "discard",		DISCARD },
		{ "exec",		EXEC },
		{ "flag",		FLAG },
		{ "flags",		FLAGS },
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
	char *buf;
	unsigned int lno;
	int c, i;

	buf = lexeme;
	lno = parser_state.lineno;

again:
	for (c = yygetc(); isspace((unsigned char)c); c = yygetc())
		continue;

	/*
	 * Macros must always be followed by `=', otherwise treat it as an
	 * unknown keyword.
	 */
	if (last_token == MACRO && c != '=')
		yyerror_at_line(lno, "unknown keyword: %s", lexeme);

	yylval.lineno = parser_state.lineno;
	yylval.number = (unsigned char)c;
	if (c == EOF)
		return 0;
	if (c == '!')
		return NEG;

	if (c == '#') {
		for (;;) {
			c = yygetc();
			if (c == '\n')
				goto again;
			if (c == EOF)
				return 0;
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
				return 0;
			}

			if (buf == lexeme + sizeof(lexeme) - 1) {
				yyerror("string too long");
				return 0;
			}
			*buf++ = c;
		}
		*buf = '\0';
		len = strlen(lexeme);
		if (len == 0)
			yyerror("empty string");
		yylval.string = strdup(lexeme);
		if (yylval.string == NULL)
			err(1, NULL);
		return STRING;
	}

	if (parser_state.pflag) {
		unsigned char delim = c;

		for (;;) {
			if (yypeek(delim))
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
		yylval.pattern.string = lexeme;

		yylval.pattern.flags = 0;
		for (;;) {
			c = yygetc();
			switch (c) {
			case 'i':
				yylval.pattern.flags |= EXPR_PATTERN_ICASE;
				break;
			case 'l':
				if (yylval.pattern.flags & EXPR_PATTERN_UCASE)
					yyerror("`l' and `u' flags cannot be combined");
				yylval.pattern.flags |= EXPR_PATTERN_LCASE;
				break;
			case 'u':
				if (yylval.pattern.flags & EXPR_PATTERN_LCASE)
					yyerror("`l' and `u' flags cannot be combined");
				yylval.pattern.flags |= EXPR_PATTERN_UCASE;
				break;
			default:
				yyungetc(c);
				return PATTERN;
			}
		}
	}

	if (isdigit((unsigned char)c)) {
		unsigned int number = 0;
		int overflow = 0;

		for (; isdigit((unsigned char)c); c = yygetc()) {
			unsigned char u = (unsigned char)c;

			if (overflow)
				continue;

			if (KS_u32_mul_overflow(number, 10, &number) ||
			    KS_u32_add_overflow(number, u - '0', &number)) {
				yyerror("integer too large");
				overflow = 1;
			}
		}
		yylval.number = number;
		yyungetc(c);
		return INT;
	}

	if (islower((unsigned char)c)) {
		for (; islower((unsigned char)c) || c == '-'; c = yygetc()) {
			if (buf == lexeme + sizeof(lexeme) - 1) {
				yyerror("keyword too long");
				return 0;
			}
			*buf++ = c;
		}
		*buf = '\0';
		yyungetc(c);

		for (i = 0; keywords[i].str != NULL; i++)
			if (strcmp(lexeme, keywords[i].str) == 0)
				return keywords[i].type;

		if (parser_state.sflag) {
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
				return SCALAR;
			} else if (match >= 0) {
				yylval.number = scalars[match].val;
				return SCALAR;
			}
		}

		yylval.string = strdup(lexeme);
		if (yylval.string == NULL)
			err(1, NULL);
		/*
		 * At this point, it's unknown if a macro is expected. An error
		 * is emitted upon the next invocation of yylex() if the macro
		 * was unexpected.
		 */
		return MACRO;
	}

	return c;
}

static void
expr_validate(const struct expr *ex)
{
	int nactions;

	if (ex == NULL) {
		yyerror("missing action");
		return;
	}

	nactions = expr_count_actions(ex);
	if (nactions > 1) {
		if (expr_count(ex, EXPR_TYPE_DISCARD) > 0)
			yyerror_at_line(ex->ex_lno,
			    "discard cannot be combined with another action");
		if (expr_count(ex, EXPR_TYPE_REJECT) > 0)
			yyerror_at_line(ex->ex_lno,
			    "reject cannot be combined with another action");
	}
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

	c = fgetc(parser_state.fh);
	if (c == '\n')
		parser_state.lineno++;
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
		parser_state.lineno--;
	ungetc(c, parser_state.fh);
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

static void
macros_validate(const struct macro_list *macros)
{
	struct macro **unused;
	size_t i;

	unused = macros_unused(macros);
	for (i = 0; i < VECTOR_LENGTH(unused); i++) {
		const struct macro *mc = unused[i];

		yyerror_at_line(macro_get_lno(mc),
		    "unused macro: %s", macro_get_name(mc));
	}
	/* coverity[leaked_storage: FALSE] */
	VECTOR_FREE(unused);
}

static char *
expand(char *str, unsigned int curctx)
{
	str = expandtilde(str, parser_state.env->ev_home);
	str = expandmacros(str, parser_state.config->cl_macros, curctx);
	return str;
}

static char *
expandmacros(char *str, struct macro_list *macros, unsigned int curctx)
{
	struct buffer *bf;
	size_t i = 0;

	bf = buffer_alloc(64);
	if (bf == NULL)
		err(1, NULL);

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
				FALLTHROUGH;
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
				macro_ref(mc);
				buffer_printf(bf, "%s", macro_get_value(mc));
			} else {
				yyerror("unknown macro used in string: %s",
				    macro);
			}

			free(macro);
			i += (size_t)n;
		} else {
fallback:
			buffer_putc(bf, str[i++]);
		}
	}
	buffer_putc(bf, '\0');

	free(str);
	str = buffer_release(bf);
	buffer_free(bf);
	return str;
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
	size_t siz = PATH_MAX;
	int n;

	if (*str != '~')
		return str;

	buf = malloc(siz);
	if (buf == NULL)
		err(1, NULL);
	n = snprintf(buf, siz, "%s%s", home, str + 1);
	if (n < 0 || (size_t)n >= siz)
		yyerror("path too long");
	free(str);
	return buf;
}
