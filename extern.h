#ifdef HAVE_QUEUE
#  include <sys/queue.h>
#else
#  include "compat-queue.h"
#endif

#include <dirent.h>
#include <limits.h>
#include <regex.h>
#include <stdio.h>
#include <time.h>

/* Annotate the argument as unused. */
#define UNUSED(x)	__##x __attribute__((__unused__))

/* Forward declarations. */
struct match_list;
struct message;

struct environment {
	char ev_home[PATH_MAX];
	char ev_hostname[HOST_NAME_MAX + 1];
	char ev_tmpdir[PATH_MAX];
	const char *ev_confpath;

	struct {
		enum {
			TZ_STATE_LOCAL,	/* getenv("TZ") == NULL */
			TZ_STATE_UTC,	/* strlen(getenv("TZ")) == 0 */
			TZ_STATE_SET,	/* strlen(getenv("TZ")) > 0 */
		} t_state;
		char t_buf[256];
		long t_offset;
	} ev_tz;

	time_t ev_now;
	pid_t ev_pid;

	unsigned int ev_options;
#define OPTION_DRYRUN	0x00000001u
#define OPTION_SYNTAX	0x00000002u
#define OPTION_STDIN	0x00000004u
};

enum subdir {
	SUBDIR_NEW,
	SUBDIR_CUR,
};

struct maildir {
	char md_root[PATH_MAX];	/* root directory */
	char md_path[PATH_MAX];	/* current directory */
	DIR *md_dir;
	struct blacklist *md_blacklist;
	enum subdir md_subdir;
	unsigned int md_flags;
#define MAILDIR_WALK	0x00000001u
#define MAILDIR_STDIN	0x00000002u
#define MAILDIR_SYNC	0x00000004u
};

struct maildir_entry {
	const char *e_dir;
	const char *e_path;
	int e_dirfd;
};

struct maildir *maildir_open(const char *, unsigned int,
    const struct environment *);

void maildir_close(struct maildir *);

int maildir_walk(struct maildir *, struct maildir_entry *);

int maildir_move(struct maildir *, const struct maildir *, struct message *,
    char *, size_t, const struct environment *);

int maildir_unlink(const struct maildir *, const struct message *);

int maildir_write(struct maildir *, const struct maildir *, struct message *,
    char *, size_t, const struct environment *);

int maildir_cmp(const struct maildir *, const struct maildir *);

struct message_flags {
	unsigned int mf_flags[2];	/* 0: uppercase, 1: lowercase */
};

char *message_flags_str(const struct message_flags *, char *, size_t);

int message_flags_isset(const struct message_flags *, unsigned char);

int message_flags_set(struct message_flags *, unsigned char, int);

struct message {
	char me_pbuf[PATH_MAX];	/* path buffer */
	const char *me_path;
	const char *me_body;
	char *me_buf;
	char *me_buf_dec;	/* decoded body */
	int me_fd;

	struct message_flags me_flags;

	struct {
		struct header *h_v;
		size_t h_nmemb;
		size_t h_size;
	} me_headers;

	TAILQ_ENTRY(message) me_entry;
};

TAILQ_HEAD(message_list, message);

struct message *message_parse(const char *, int, const char *);

void message_free(struct message *);

int message_writeat(struct message *, int, unsigned int);

int message_get_fd(const struct message *);

const char *message_get_body(struct message *);

const struct string_list *message_get_header(const struct message *,
    const char *);

const char *message_get_header1(const struct message *, const char *);

void message_set_header(struct message *, const char *, char *);

int message_get_attachments(const struct message *, struct message_list **);

void message_list_free(struct message_list *);

enum expr_type {
	/* logical operators */
	EXPR_TYPE_BLOCK = 0,
	EXPR_TYPE_AND = 1,
	EXPR_TYPE_OR = 2,
	EXPR_TYPE_NEG = 3,

	/* matchers */
	EXPR_TYPE_ALL = 4,
	EXPR_TYPE_ATTACHMENT = 5,
	EXPR_TYPE_BODY = 6,
	EXPR_TYPE_DATE = 7,
	EXPR_TYPE_HEADER = 8,
	EXPR_TYPE_NEW = 9,
	EXPR_TYPE_OLD = 10,

	/* actions */
	EXPR_TYPE_MOVE = 11,
	EXPR_TYPE_FLAG = 12,
	EXPR_TYPE_DISCARD = 13,
	EXPR_TYPE_BREAK = 14,
	EXPR_TYPE_LABEL = 15,
	EXPR_TYPE_PASS = 16,
	EXPR_TYPE_REJECT = 17,
	EXPR_TYPE_EXEC = 18,
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

struct expr {
	enum expr_type ex_type;
	int ex_lno;
	unsigned int ex_flags;
#define EXPR_FLAG_ACTION	0x00000001u
#define EXPR_FLAG_INSPECT	0x00000002u
#define EXPR_FLAG_MATCH		0x00000004u
#define EXPR_FLAG_INTERPOLATE	0x00000008u
#define EXPR_FLAG_PATH		0x00000010u

	int (*ex_eval)(struct expr *, struct match_list *, struct message *,
			const struct environment *);

	struct string_list *ex_strings;

	struct {
		regex_t r_pattern;
		regmatch_t *r_matches;
		size_t r_nmatches;
		unsigned int r_flags;
#define EXPR_PATTERN_ICASE	0x00000001u
#define EXPR_PATTERN_FORCE	0x00000002u
#define EXPR_PATTERN_LCASE	0x00000004u
#define EXPR_PATTERN_UCASE	0x00000008u
	} ex_re;

	union {
		struct {
			enum expr_date_cmp d_cmp;
			enum expr_date_field d_field;
			time_t d_age;
		} u_date;

		struct {
			unsigned int e_flags;
#define EXPR_EXEC_STDIN	0x00000001u
		} u_exec;
	} ex_u;
#define ex_date	ex_u.u_date
#define ex_exec	ex_u.u_exec

	struct match *ex_match;

	struct expr *ex_lhs;
	struct expr *ex_rhs;
};

/* Return values for expr_eval(). */
#define EXPR_MATCH	0
#define EXPR_NOMATCH	1
#define EXPR_ERROR	-1

struct match {
	char mh_path[PATH_MAX];
	char mh_maildir[PATH_MAX];
	char mh_subdir[NAME_MAX + 1];

	const struct expr *mh_expr;

	char **mh_matches;
	size_t mh_nmatches;
#define mh_exec		mh_matches
#define mh_nexec	mh_nmatches

	char *mh_key;
	char *mh_val;

	TAILQ_ENTRY(match) mh_entry;
};

TAILQ_HEAD(match_list, match);

struct expr *expr_alloc(enum expr_type, int, struct expr *, struct expr *);

void expr_free(struct expr *);

void expr_set_date(struct expr *, enum expr_date_field, enum expr_date_cmp,
    time_t);

void expr_set_exec(struct expr *, struct string_list *, unsigned int);

void expr_set_strings(struct expr *, struct string_list *);

int expr_set_pattern(struct expr *, const char *, unsigned int, const char **);

int expr_count(const struct expr *, enum expr_type);

int expr_count_actions(const struct expr *);

int expr_count_patterns(const struct expr *, unsigned int);

int expr_eval(struct expr *, struct match_list *, struct message *,
    const struct environment *);

void expr_inspect(const struct expr *, FILE *, const struct environment *);

int matches_append(struct match_list *, struct match *, const struct message *);

void matches_clear(struct match_list *);

int matches_interpolate(struct match_list *, struct message *);

int matches_exec(const struct match_list *, struct maildir *, struct message *,
    int *, const struct environment *);

int matches_inspect(const struct match_list *, const struct message *, FILE *,
    const struct environment *);

void match_copy(struct match *, const char *, const regmatch_t *, size_t);

void match_reset(struct match *);

struct match *matches_find(struct match_list *, enum expr_type);

int matches_remove(struct match_list *, enum expr_type);

char *time_format(time_t, char *, size_t);

int time_parse(const char *, time_t *, const struct environment *);

struct string {
	char *val;

	TAILQ_ENTRY(string) entry;
};

TAILQ_HEAD(string_list, string);

struct string_list *strings_alloc(void);

void strings_free(struct string_list *);

size_t strings_len(const struct string_list *);

void strings_append(struct string_list *, char *);

struct macro {
	char *mc_name;
	char *mc_value;
	unsigned int mc_refs;
	unsigned int mc_lno;
	unsigned int mc_flags;
#define MACRO_FLAG_STATIC	0x00000001u

	TAILQ_ENTRY(macro) mc_entry;
};

struct macro_list {
	struct macro ml_v[2];
	size_t ml_nmemb;
	size_t ml_size;

	TAILQ_HEAD(, macro) ml_list;
};

void macros_init(struct macro_list *);
int macros_insert(struct macro_list *, char *, char *, int);
struct macro *macros_find(struct macro_list *, const char *);
ssize_t ismacro(const char *, char **);

struct config {
	struct {
		char *path;
		unsigned int flags;
	} maildir;
	struct expr *expr;

	TAILQ_ENTRY(config) entry;
};

struct config_list {
	struct macro_list *cf_macros;

	TAILQ_HEAD(, config) cf_list;
};

struct config_list *config_parse(const char *, const struct environment *);

void config_free(struct config_list *);

char *pathjoin(char *, size_t, const char *, const char *);
char *pathslice(const char *, char *, size_t, int, int);

size_t nspaces(const char *);

size_t append(char **, size_t *, size_t *, const char *);
size_t appendc(char **, size_t *, size_t *, char);

extern int log_level;

#define log_info(fmt, ...) do {						\
	if (log_level >= 1)						\
		_log_info((fmt), __VA_ARGS__);				\
} while (0)

#define log_debug(fmt, ...) do {					\
	if (log_level >= 2)						\
		_log_debug((fmt), __VA_ARGS__);				\
} while (0)

void _log_debug(const char *fmt, ...)
	__attribute__((__format__ (printf, 1, 2)));

void _log_info(const char *fmt, ...)
	__attribute__((__format__ (printf, 1, 2)));
