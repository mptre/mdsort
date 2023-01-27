#ifdef HAVE_QUEUE
#  include <sys/queue.h>
#else
#  include "compat-queue.h"
#endif

#include <limits.h>
#include <regex.h>
#include <time.h>

#include "fault.h"

/*
 * environment -----------------------------------------------------------------
 */

struct environment {
	char		 ev_home[PATH_MAX];
	char		 ev_hostname[HOST_NAME_MAX + 1];
	char		 ev_tmpdir[PATH_MAX];
	const char	*ev_confpath;

	struct {
		enum {
			TZ_STATE_LOCAL,	/* getenv("TZ") == NULL */
			TZ_STATE_UTC,	/* strlen(getenv("TZ")) == 0 */
			TZ_STATE_SET,	/* strlen(getenv("TZ")) > 0 */
		} t_state;
		char	t_buf[256];
		long	t_offset;
	} ev_tz;

	time_t		 ev_now;
	pid_t		 ev_pid;

	unsigned int	 ev_options;
#define OPTION_DRYRUN	0x00000001u
#define OPTION_SYNTAX	0x00000002u
#define OPTION_STDIN	0x00000004u
};

/*
 * maildir ---------------------------------------------------------------------
 */

struct message;

/* Flags passed to maildir_open(). */
#define MAILDIR_WALK	0x00000001u
#define MAILDIR_STDIN	0x00000002u

struct maildir_entry {
	const char	*e_dir;
	const char	*e_path;
	int		 e_dirfd;
};

struct maildir	*maildir_open(const char *, unsigned int,
    const struct environment *);
void		 maildir_close(struct maildir *);

int	maildir_walk(struct maildir *, struct maildir_entry *);
int	maildir_move(struct maildir *, const struct maildir *,
    struct message *, const struct environment *);
int	maildir_unlink(const struct maildir *, const char *);
int	maildir_write(struct maildir *, struct message *,
    const struct environment *);

int	maildir_cmp(const struct maildir *, const struct maildir *);

/*
 * expr ------------------------------------------------------------------------
 */

struct match;
struct match_list;

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

	const char		*ex_label;

	struct string_list	*ex_strings;

	struct {
		regex_t		 pattern;
		regmatch_t	*matches;
		size_t		 nmatches;
		unsigned int	 flags;
#define EXPR_PATTERN_ICASE	0x00000001u
#define EXPR_PATTERN_LCASE	0x00000002u
#define EXPR_PATTERN_UCASE	0x00000004u
	} ex_re;

	union {
		struct {
			enum expr_date_cmp	cmp;
			enum expr_date_field	field;
			time_t			age;
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

/* Return values for expr_eval(). */
#define EXPR_MATCH	(0)
#define EXPR_NOMATCH	(1)
#define EXPR_ERROR	(-1)

struct expr	*expr_alloc(enum expr_type, unsigned int, struct expr *,
    struct expr *);
void		 expr_free(struct expr *);

void	expr_set_add_header(struct expr *, char *, char *);
void	expr_set_date(struct expr *, enum expr_date_field, enum expr_date_cmp,
    time_t);
int	expr_set_exec(struct expr *, struct string_list *, unsigned int);
void	expr_set_stat(struct expr *, char *, enum expr_stat);
void	expr_set_strings(struct expr *, struct string_list *);
int	expr_set_pattern(struct expr *, const char *, unsigned int,
    const char **);

int	expr_count(const struct expr *, enum expr_type);
int	expr_count_actions(const struct expr *);

int	expr_eval(struct expr *, struct expr_eval_arg *);

void	expr_inspect(const struct expr *, const struct match *,
    const struct environment *);

/*
 * match ---------------------------------------------------------------------
 */

struct macro_list;

struct match {
	char			  mh_path[PATH_MAX];
	char			  mh_maildir[PATH_MAX];
	char			  mh_subdir[NAME_MAX + 1];

	const struct expr	 *mh_expr;
	struct message		 *mh_msg;

	struct {
		char	*m_str;
		size_t	 m_beg;
		size_t	 m_end;
	} *mh_matches;
	size_t			  mh_nmatches;

	char			**mh_exec;
	size_t			  mh_nexec;

	char			 *mh_key;
	char			 *mh_val;

	TAILQ_ENTRY(match)	  mh_entry;
};

TAILQ_HEAD(match_list, match);

int	matches_append(struct match_list *, struct match *);
void	matches_clear(struct match_list *);
int	matches_interpolate(struct match_list *);
int	matches_exec(const struct match_list *, struct maildir *, int *,
    const struct environment *);
int	matches_inspect(const struct match_list *, const struct environment *);

struct match	*match_alloc(struct expr *, struct message *);
void		 match_free(struct match *);

void		 match_copy(struct match *, const char *, const regmatch_t *,
    size_t);
int		 match_interpolate(struct match *, const struct macro_list *);
struct match	*matches_find(struct match_list *, enum expr_type);
int		 matches_remove(struct match_list *, enum expr_type);

/*
 * time ------------------------------------------------------------------------
 */

char	*time_format(time_t, char *, size_t);
int	 time_parse(const char *, time_t *, const struct environment *);

/*
 * string ----------------------------------------------------------------------
 */

struct string {
	char			*val;
	unsigned int		 flags;
#define STRING_FLAG_CONST	0x00000001u

	TAILQ_ENTRY(string)	 entry;
};

TAILQ_HEAD(string_list, string);

struct string_list	*strings_alloc(void);
void			 strings_free(struct string_list *);

struct string	*strings_append(struct string_list *, char *);
struct string	*strings_appendc(struct string_list *, const char *);
char		*strings_concat(const struct string_list *, char *, size_t *,
    size_t *, char);

size_t	strings_len(const struct string_list *);

/*
 * macros ----------------------------------------------------------------------
 */

#define MACRO_CTX_DEFAULT	0x00000001u
#define MACRO_CTX_ACTION	0x00000002u

enum macro_error {
	MACRO_ERR_NONE,
	MACRO_ERR_CTX,
	MACRO_ERR_EXIST,
	MACRO_ERR_STICKY,
};

struct macro {
	char			*mc_name;
	char			*mc_value;
	unsigned int		 mc_refs;
	unsigned int		 mc_defs;
	unsigned int		 mc_lno;
	unsigned int		 mc_flags;
#define MACRO_FLAG_STATIC	0x00000001u	/* storage provided by ml_v */
#define MACRO_FLAG_CONST	0x00000002u	/* may not be freed */
#define MACRO_FLAG_STICKY	0x00000004u	/* may not be overwritten */

	TAILQ_ENTRY(macro)	 mc_entry;
};

struct macro_list {
	struct macro		ml_v[2];
	size_t			ml_nmemb;
	size_t			ml_size;
	unsigned int		ml_ctx;

	TAILQ_HEAD(, macro)	ml_list;
};

void		 macros_init(struct macro_list *, unsigned int);
enum macro_error macros_insert(struct macro_list *, char *, char *,
    unsigned int, unsigned int);
void		 macros_insertc(struct macro_list *, const char *,
    const char *);
struct macro	*macros_find(const struct macro_list *, const char *);
unsigned int	 macro_context(const char *);

ssize_t	ismacro(const char *, char **);

/*
 * config ----------------------------------------------------------------------
 */

struct config {
	struct string_list	*paths;
	struct expr		*expr;

	TAILQ_ENTRY(config)	 entry;
};

struct config_list {
	struct macro_list	cl_macros;

	TAILQ_HEAD(, config)	cl_list;
};

void	config_init(struct config_list *);
int	config_parse(struct config_list *, const char *,
    const struct environment *);
void	config_free(struct config_list *);

/*
 * util ------------------------------------------------------------------------
 */

int	exec(char *const *, int);

char	*pathjoin(char *, size_t, const char *, const char *);
char	*pathslice(const char *, char *, size_t, int, int);

size_t	nspaces(const char *);

size_t	append(char **, size_t *, size_t *, const char *);
size_t	appendc(char **, size_t *, size_t *, char);

int	isstdin(const char *);

extern int	log_level;

#define log_info(fmt, ...) do {						\
	if (log_level >= 1)						\
		logit((fmt), __VA_ARGS__);				\
} while (0)

#define log_debug(fmt, ...) do {					\
	if (log_level >= 2)						\
		logit((fmt), __VA_ARGS__);				\
} while (0)

void	logit(const char *fmt, ...)
	__attribute__((__format__(printf, 1, 2)));
