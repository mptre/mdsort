struct arena_scope;
struct match;

/* Return values for expr_eval(). */
#define EXPR_MATCH	(0)
#define EXPR_NOMATCH	(1)
#define EXPR_ERROR	(-1)

/* expr_set_pattern() flags */
#define EXPR_PATTERN_ICASE	0x00000001u
#define EXPR_PATTERN_LCASE	0x00000002u
#define EXPR_PATTERN_UCASE	0x00000004u

enum expr_type {
	/* blocks */
	EXPR_TYPE_BLOCK,

	/* logical operators */
	EXPR_TYPE_AND,
	EXPR_TYPE_OR,
	EXPR_TYPE_NEG,
	EXPR_TYPE_MATCH,	/* alias for and */

	/* matchers */
	EXPR_TYPE_ALL,
	EXPR_TYPE_ATTACHMENT,
	EXPR_TYPE_BODY,
	EXPR_TYPE_DATE,
	EXPR_TYPE_HEADER,
	EXPR_TYPE_NEW,
	EXPR_TYPE_OLD,
	EXPR_TYPE_STAT,
	EXPR_TYPE_COMMAND,

	/* actions */
	EXPR_TYPE_MOVE,
	EXPR_TYPE_FLAG,
	EXPR_TYPE_FLAGS,
	EXPR_TYPE_DISCARD,
	EXPR_TYPE_BREAK,
	EXPR_TYPE_LABEL,
	EXPR_TYPE_PASS,
	EXPR_TYPE_REJECT,
	EXPR_TYPE_EXEC,
	EXPR_TYPE_ATTACHMENT_BLOCK,
	EXPR_TYPE_ADD_HEADER,
};

enum expr_date_cmp {
	EXPR_DATE_CMP_LT,
	EXPR_DATE_CMP_GT,
};

enum expr_date_field {
	EXPR_DATE_FIELD_HEADER,
	EXPR_DATE_FIELD_ACCESS,
	EXPR_DATE_FIELD_MODIFIED,
	EXPR_DATE_FIELD_CREATED,
};

enum expr_stat {
	EXPR_STAT_DIR,
};

struct expr_eval_arg {
	struct match_list		*ea_ml;
	struct message			*ea_msg;
	const struct environment	*ea_env;
};

struct expr {
	enum expr_type		 ex_type;
	unsigned int		 ex_lno;
	unsigned int		 ex_flags;
/* Denotes an action. */
#define EXPR_FLAG_ACTION	0x00000001u
/* Associated with a match that must be displayed during dry run. */
#define EXPR_FLAG_INSPECT	0x00000002u
/* Associated with a match that can be used during interpolation. */
#define EXPR_FLAG_INTERPOLATE	0x00000004u
/* Associated with a match that requires a maildir destination path. */
#define EXPR_FLAG_PATH		0x00000008u

	int			 (*ex_eval)(struct expr *,
	    struct expr_eval_arg *);
	const char		*(*ex_inspect)(const struct expr *,
	    const struct match *, const struct message *, struct arena_scope *);

	const char		*ex_label;

	struct string_list	*ex_strings;

	struct expr_regex	*ex_re;

	union {
		struct {
			enum expr_date_cmp	cmp;
			enum expr_date_field	field;
			long long int		age;
		} ex_date;

		struct {
			unsigned int	flags;
#define EXPR_EXEC_STDIN	0x00000001u
#define EXPR_EXEC_BODY	0x00000002u
		} ex_exec;

		struct {
			enum expr_stat	stat;
		} ex_stat;

		struct {
			char *key;
			char *val;
		} ex_add_header;
	};

	struct expr		*ex_lhs;
	struct expr		*ex_rhs;
};

struct expr	*expr_alloc(enum expr_type, unsigned int, struct expr *,
    struct expr *);
void		 expr_free(struct expr *);

void	expr_set_add_header(struct expr *, char *, char *);
void	expr_set_date(struct expr *, enum expr_date_field, enum expr_date_cmp,
    long long int);
int	expr_set_exec(struct expr *, struct string_list *, unsigned int);
void	expr_set_stat(struct expr *, char *, enum expr_stat);
void	expr_set_strings(struct expr *, struct string_list *);
int	expr_set_pattern(struct expr *, const char *, unsigned int,
    const char **);

int	expr_count(const struct expr *, enum expr_type);
int	expr_count_actions(const struct expr *);

int	expr_eval(struct expr *, struct expr_eval_arg *);

const char	*expr_inspect(const struct expr *, const struct match *,
    const struct message *, struct arena_scope *);
void		 expr_inspect_matches(const struct expr *, const struct match *,
    const struct environment *);
