#ifdef HAVE_QUEUE
#  include <sys/queue.h>
#else
#  include "compat-queue.h"
#endif

#include <dirent.h>
#include <limits.h>
#include <regex.h>
#include <stdio.h>

/* Forward declarations. */
struct environment;
struct message;

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

/*
 * Open the maildir directory located at path.
 *
 * The flags may be any combination of the following values:
 *
 *     MAILDIR_WALK      Invoking maildir_walk() will traverse all messages
 *                       present in the cur and new subdirectories rooted at
 *                       path.
 *
 *     MAILDIR_ROOT      The given path refers to a maildir root, as opposed of
 *                       referencing a subdirectory (cur, new or tmp).
 *
 *     MAILDIR_STDIN     Read messages from stdin.
 *
 * The caller is responsible for freeing the returned memory using
 * maildir_close().
 */
struct maildir *maildir_open(const char *path, int flags,
    const struct environment *env);

#define MAILDIR_WALK	0x1
#define MAILDIR_ROOT	0x2
#define MAILDIR_STDIN	0x4

void maildir_close(struct maildir *md);

/*
 * Returns the path to the next file located in the maildir.
 * Calling it repeatedly will traverse all the files.
 * Once all files have been traversed, NULL is returned.
 */
const char *maildir_walk(struct maildir *md);

/*
 * Move the message located in src to dst.
 *
 * Returns zero on success, non-zero otherwise.
 */
int maildir_move(const struct maildir *src, const struct maildir *dst,
    struct message *msg, const struct environment *env);

/*
 * Remove the message located in the given maildir.
 *
 * Returns zero on success, non-zero otherwise.
 */
int maildir_unlink(const struct maildir *md, const struct message *msg);

struct message {
	const char *path;
	const char *body;
	char *buf;

	unsigned int flags;

	struct header *headers;
	size_t nheaders;
};

/*
 * Parse the message located at path.
 *
 * The caller is responsible for freeing the returned memory using
 * message_free().
 */
struct message *message_parse(const char *path);

void message_free(struct message *msg);

const struct string_list *message_get_header(const struct message *msg,
    const char *header);

const char *message_get_flags(const struct message *msg);

int message_has_flags(const struct message *msg, unsigned char flag);

void message_set_flags(struct message *msg, unsigned char flag, int add);

enum expr_type {
	EXPR_TYPE_BLOCK,
	EXPR_TYPE_AND,
	EXPR_TYPE_OR,
	EXPR_TYPE_NEG,
	EXPR_TYPE_ALL,
	EXPR_TYPE_BODY,
	EXPR_TYPE_HEADER,
	EXPR_TYPE_NEW,
	EXPR_TYPE_OLD,
	EXPR_TYPE_MOVE,
	EXPR_TYPE_FLAG,
	EXPR_TYPE_DISCARD,
	EXPR_TYPE_PASS,
};

struct expr {
	enum expr_type type;
	int cookie;

	struct string_list *strings;

	regex_t pattern;
	regmatch_t *matches;
	size_t nmatches;

	struct match *match;

	struct expr *lhs;
	struct expr *rhs;
};

struct match {
	char maildir[PATH_MAX];
	char subdir[NAME_MAX];
	char path[PATH_MAX];

	const struct expr *action;

	char **matches;
	size_t nmatches;

	const char *key;
	const char *val;
};

/*
 * Allocate a new expression with the given type.
 *
 * The caller is responsible for freeing the returned memory using
 * expr_free().
 */
struct expr *expr_alloc(enum expr_type type, struct expr *lhs,
    struct expr *rhs);

void expr_free(struct expr *ex);

/*
 * Associate the given string list with the expression.
 */
void expr_set_strings(struct expr *ex, struct string_list *strings);

/*
 * Associate the given pattern with the expression.
 *
 * The flags may be any combination of the following values:
 *
 *     EXPR_PATTERN_ICASE    Ignore case.
 *
 * Returns zero if pattern is successfully compiled into a regular expression.
 * Otherwise, returns non-zero and if errstr is not NULL it will point to an
 * explanation on why the compilation failed.
 */
int expr_set_pattern(struct expr *ex, const char *pattern, int flags,
    const char **errstr);

#define EXPR_PATTERN_ICASE	0x1

/*
 * Returns the number of expressions with the given type.
 */
int expr_count(const struct expr *ex, enum expr_type type);

/*
 * Returns the number of actions.
 */
int expr_count_actions(const struct expr *ex);

/*
 * Returns a match if the expression matches the given message.
 * Otherwise, NULL is returned.
 */
const struct match *expr_eval(struct expr *ex, const struct message *msg);

/*
 * Writes a human readable representation of the latest match to fh.
 */
void expr_inspect(const struct expr *ex, FILE *fh);

struct string {
	char *val;

	TAILQ_ENTRY(string) entry;
};

TAILQ_HEAD(string_list, string);

/*
 * Allocate a list of strings.
 *
 * The caller is responsible for freeing the returned memory using
 * strings_free().
 */
struct string_list *strings_alloc(void);

void strings_free(struct string_list *strings);

void strings_append(struct string_list *strings, char *val);

struct config {
	char *maildir;
	struct expr *expr;

	TAILQ_ENTRY(config) entry;
};

TAILQ_HEAD(config_list, config);

struct environment {
	char home[PATH_MAX];
	char hostname[HOST_NAME_MAX + 1];
	char tmpdir[PATH_MAX];
};

/*
 * Parses the configuration located at path and returns a config list on
 * success.
 * Otherwise, NULL is returned.
 */
struct config_list *parse_config(const char *path,
    const struct environment *env);

/*
 * Join root, dirname and filename into a path written to buf which must be at
 * least of size PATH_MAX.
 * The filename may optionally be NULL.
 */
char *pathjoin(char *buf, const char *root, const char *dirname,
    const char *filename);

/*
 * Writes the given number of components from path to buf which must be at least
 * of size PATH_MAX.
 * The component range as given by beg and end may either be positive (start
 * from the beginning) or negative (start from the end).
 * If beg is equal to end, only a single component of the path is extract.
 */
char *pathslice(const char *path, char *buf, int beg, int end);

void log_init(int v);

void log_debug(const char *fmt, ...)
	__attribute__((__format__ (printf, 1, 2)));

void log_info(const char *fmt, ...)
	__attribute__((__format__ (printf, 1, 2)));
