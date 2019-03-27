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

#define UNUSED(x)	_##x __attribute__((__unused__))

/* Forward declarations. */
struct message;

struct environment {
	char home[PATH_MAX];
	char hostname[HOST_NAME_MAX + 1];
	char tmpdir[PATH_MAX];
	const char *confpath;

	enum {
		TZ_STATE_LOCAL,	/* getenv("TZ") == NULL */
		TZ_STATE_UTC,	/* strlen(getenv("TZ")) == 0 */
		TZ_STATE_SET,	/* strlen(getenv("TZ")) > 0 */
	} tz_state;
	char tz_buf[256];
	long tz_offset;
	time_t now;

	unsigned int options;
#define OPTION_DRYRUN	0x1
#define OPTION_SYNTAX	0x2
};

enum subdir {
	SUBDIR_NEW,
	SUBDIR_CUR,
};

struct maildir {
	char *path;
	DIR *dir;
	enum subdir subdir;
	int flags;
	char buf[PATH_MAX];
};

#define MAILDIR_WALK	0x1
#define MAILDIR_ROOT	0x2
#define MAILDIR_STDIN	0x4

struct maildir *maildir_open(const char *path, int flags,
    const struct environment *env);

void maildir_close(struct maildir *md);

const char *maildir_walk(struct maildir *md);

int maildir_move(const struct maildir *src, const struct maildir *dst,
    struct message *msg, const struct environment *env);

int maildir_unlink(const struct maildir *md, const struct message *msg);

int maildir_write(const struct maildir *src, const struct maildir *dst,
    struct message *msg, char *buf, size_t bufsiz,
    const struct environment *env);

struct message {
	const char *path;
	const char *body;
	char *buf;

	unsigned int flags;

	struct header *headers;
	size_t nheaders;

	TAILQ_ENTRY(message) entry;
};

TAILQ_HEAD(message_list, message);

struct message *message_parse(const char *path);

void message_free(struct message *msg);

int message_writeat(struct message *msg, int dirfd, const char *path);

const struct string_list *message_get_header(const struct message *msg,
    const char *header);

const char *message_get_header1(const struct message *msg, const char *header);

void message_set_header(struct message *msg, const char *header, char *val);

const char *message_get_flags(const struct message *msg);

int message_has_flags(const struct message *msg, unsigned char flag);

void message_set_flags(struct message *msg, unsigned char flag, int add);

int message_has_label(const struct message *msg, const char *label);

struct message_list *message_get_attachments(const struct message *msg);

void message_list_free(struct message_list *messages);

enum expr_type {
	EXPR_TYPE_BLOCK,
	EXPR_TYPE_AND,
	EXPR_TYPE_OR,
	EXPR_TYPE_NEG,
	EXPR_TYPE_ALL,
	EXPR_TYPE_ATTACHMENT,
	EXPR_TYPE_BODY,
	EXPR_TYPE_DATE,
	EXPR_TYPE_HEADER,
	EXPR_TYPE_NEW,
	EXPR_TYPE_OLD,

	/* actions */
	EXPR_TYPE_MOVE,
	EXPR_TYPE_FLAG,
	EXPR_TYPE_DISCARD,
	EXPR_TYPE_BREAK,
	EXPR_TYPE_LABEL,
};

enum expr_cmp {
	EXPR_CMP_LT,
	EXPR_CMP_GT,
};

struct expr {
	enum expr_type type;
	int lno;

	struct string_list *strings;

	struct {
		regex_t r_pattern;
		regmatch_t *r_matches;
		size_t r_nmatches;
		unsigned int r_flags;
#define EXPR_PATTERN_ICASE	0x1
#define EXPR_PATTERN_FORCE	0x2
	} ex_re;

	struct {
		enum expr_cmp cmp;
		time_t age;
	} date;

	struct match *match;

	struct expr *lhs;
	struct expr *rhs;
};

struct match {
	const struct expr *mh_expr;

	char **mh_matches;
	size_t mh_nmatches;

	char *mh_key;
	char *mh_val;

	TAILQ_ENTRY(match) mh_entry;
};

struct match_list {
	char ml_maildir[PATH_MAX];
	char ml_subdir[PATH_MAX];
	char ml_path[PATH_MAX];

	TAILQ_HEAD(, match) ml_head;
};

#define MATCH_LIST_INITIALIZER(ml)	\
	{"", "", "", TAILQ_HEAD_INITIALIZER((ml).ml_head) }

struct expr *expr_alloc(enum expr_type type, int lno, struct expr *lhs,
    struct expr *rhs);

void expr_free(struct expr *ex);

void expr_set_date(struct expr *ex, enum expr_cmp cmp, time_t age);

void expr_set_strings(struct expr *ex, struct string_list *strings);

int expr_set_pattern(struct expr *ex, const char *pattern, int flags,
    const char **errstr);

int expr_count(const struct expr *ex, enum expr_type type);

int expr_count_actions(const struct expr *ex);

int expr_count_patterns(const struct expr *ex, unsigned int flags);

int expr_eval(struct expr *ex, struct match_list *ml, struct message *msg,
    const struct environment *env);

void expr_inspect(const struct expr *ex, FILE *fh,
    const struct environment *env);

void matches_append(struct match_list *ml, struct match *mh);

void matches_clear(struct match_list *ml);

int matches_interpolate(struct match_list *ml, struct message *msg);

int matches_exec(const struct match_list *ml, struct maildir *md,
    struct message *msg, const struct environment *env);

void matches_inspect(const struct match_list *ml, FILE *fh,
    const struct environment *env);

void match_copy(struct match *mh, const char *str, const regmatch_t *off,
    size_t nmemb);

void match_reset(struct match *mh);

struct match *matches_find(struct match_list *ml, enum expr_type type);

int time_parse(const char *str, time_t *res, const struct environment *env);

struct string {
	char *val;

	TAILQ_ENTRY(string) entry;
};

TAILQ_HEAD(string_list, string);

struct string_list *strings_alloc(void);

void strings_free(struct string_list *strings);

void strings_append(struct string_list *strings, char *val);

struct config {
	char *maildir;
	struct expr *expr;

	TAILQ_ENTRY(config) entry;
};

TAILQ_HEAD(config_list, config);

struct config_list *parse_config(const char *path,
    const struct environment *env);

char *pathjoin(char *buf, const char *root, const char *dirname,
    const char *filename);

char *pathslice(const char *path, char *buf, int beg, int end);

void log_init(int v);

void log_debug(const char *fmt, ...)
	__attribute__((__format__ (printf, 1, 2)));

void log_info(const char *fmt, ...)
	__attribute__((__format__ (printf, 1, 2)));
