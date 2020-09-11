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

/*
 * Annotate the argument as unused.
 */
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
	char md_buf[PATH_MAX];
	char md_path[PATH_MAX];
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

struct maildir *maildir_open(const char *path, unsigned int flags,
    const struct environment *env);

void maildir_close(struct maildir *md);

int maildir_walk(struct maildir *md, struct maildir_entry *me);

int maildir_move(struct maildir *src, const struct maildir *dst,
    struct message *msg, char *buf, size_t bufsiz,
    const struct environment *env);

int maildir_unlink(const struct maildir *md, const struct message *msg);

int maildir_write(struct maildir *src, const struct maildir *dst,
    struct message *msg, char *buf, size_t bufsiz,
    const struct environment *env);

int maildir_cmp(const struct maildir *md1, const struct maildir *md2);

struct message_flags {
	unsigned int mf_flags[2];	/* 0: uppercase, 1: lowercase */
};

char *message_flags_str(const struct message_flags *flags, char *buf,
    size_t bufsiz);

int message_flags_isset(const struct message_flags *flags,
    unsigned char flag);

int message_flags_set(struct message_flags *flags, unsigned char flag,
    int add);

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

struct message *message_parse(const char *dir, int dirfd, const char *path);

void message_free(struct message *msg);

int message_writeat(struct message *msg, int fd, unsigned int dosync);

int message_get_fd(const struct message *msg);

const char *message_get_body(struct message *msg);

const struct string_list *message_get_header(const struct message *msg,
    const char *header);

const char *message_get_header1(const struct message *msg, const char *header);

void message_set_header(struct message *msg, const char *header, char *val);

int message_get_attachments(const struct message *msg,
    struct message_list **attachments);

void message_list_free(struct message_list *messages);

enum expr_type {
	EXPR_TYPE_BLOCK = 0,
	EXPR_TYPE_AND = 1,
	EXPR_TYPE_OR = 2,
	EXPR_TYPE_NEG = 3,
	EXPR_TYPE_ALL = 4,
	EXPR_TYPE_ATTACHMENT = 5,
	EXPR_TYPE_ATTACHMENT_BODY = 6,
	EXPR_TYPE_ATTACHMENT_HEADER = 7,
	EXPR_TYPE_BODY = 8,
	EXPR_TYPE_DATE = 9,
	EXPR_TYPE_HEADER = 10,
	EXPR_TYPE_NEW = 11,
	EXPR_TYPE_OLD = 12,

	/* actions */
	EXPR_TYPE_MOVE = 13,
	EXPR_TYPE_FLAG = 14,
	EXPR_TYPE_DISCARD = 15,
	EXPR_TYPE_BREAK = 16,
	EXPR_TYPE_LABEL = 17,
	EXPR_TYPE_PASS = 18,
	EXPR_TYPE_REJECT = 19,
	EXPR_TYPE_EXEC = 20,
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

struct expr *expr_alloc(enum expr_type type, int lno, struct expr *lhs,
    struct expr *rhs);

void expr_free(struct expr *ex);

void expr_set_date(struct expr *ex, enum expr_date_field field,
    enum expr_date_cmp cmp, time_t age);

void expr_set_exec(struct expr *ex, struct string_list *cmd,
    unsigned int flags);

void expr_set_strings(struct expr *ex, struct string_list *strings);

int expr_set_pattern(struct expr *ex, const char *pattern, unsigned int flags,
    const char **errstr);

int expr_count(const struct expr *ex, enum expr_type type);

int expr_count_actions(const struct expr *ex);

int expr_count_patterns(const struct expr *ex, unsigned int flags);

int expr_eval(struct expr *ex, struct match_list *ml, struct message *msg,
    const struct environment *env);

void expr_inspect(const struct expr *ex, FILE *fh,
    const struct environment *env);

int matches_append(struct match_list *ml, struct match *mh,
    const struct message *msg);

void matches_clear(struct match_list *ml);

int matches_interpolate(struct match_list *ml, struct message *msg);

int matches_exec(const struct match_list *ml, struct maildir *src,
    struct message *msg, int *reject, const struct environment *env);

int matches_inspect(const struct match_list *ml, const struct message *msg,
    FILE *fh, const struct environment *env);

void match_copy(struct match *mh, const char *str, const regmatch_t *off,
    size_t nmemb);

void match_reset(struct match *mh);

struct match *matches_find(struct match_list *ml, enum expr_type type);

int matches_remove(struct match_list *ml, enum expr_type type);

char *time_format(time_t tim, char *buf, size_t bufsiz);

int time_parse(const char *str, time_t *res, const struct environment *env);

struct string {
	char *val;

	TAILQ_ENTRY(string) entry;
};

TAILQ_HEAD(string_list, string);

struct string_list *strings_alloc(void);

void strings_free(struct string_list *strings);

size_t strings_len(const struct string_list *strings);

void strings_append(struct string_list *strings, char *val);

struct config {
	struct {
		char *path;
		unsigned int flags;
	} maildir;
	struct expr *expr;

	TAILQ_ENTRY(config) entry;
};

struct config_list {
    TAILQ_HEAD(, config) cf_list;
};

struct config_list *config_parse(const char *path,
    const struct environment *env);

void config_free(struct config_list *config);

char *pathjoin(char *buf, size_t bufsiz, const char *dirname,
    const char *filename);

char *pathslice(const char *path, char *buf, size_t bufsiz, int beg, int end);

size_t nspaces(const char *str);

size_t append(char **buf, size_t *bufsiz, size_t *buflen, const char *str);
size_t appendc(char **buf, size_t *bufsiz, size_t *buflen, char ch);

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
