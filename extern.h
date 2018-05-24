#ifdef HAVE_QUEUE
#  include <sys/queue.h>
#else
#  include "compat-queue.h"
#endif

/*
 * Forwards declarations.
 */
struct expr;
struct expr_headers;

#define MAILDIR_WALK	0x1
#define MAILDIR_CREATE	0x2

/*
 * Open the maildir directory located at path.
 *
 * The flags may be any combination of the following values:
 *
 *     MAILDIR_WALK      Invoking maildir_walk() will traverse all messages
 *                       present in the cur and new subdirectories rooted at
 *                       path.
 *
 *     MAILDIR_CREATE    Create the maildir and subdirectories if missing.
 *
 * The caller is responsible for freeing the returned memory using
 * maildir_close().
 */
struct maildir *maildir_open(const char *path, int flags);

/*
 * Close and free maildir.
 */
void maildir_close(struct maildir *md);

/*
 * Returns the path to the next file located in the given maildir.
 * Calling it repeatedly will traverse all the files.
 * Upon reaching the end of the maildir, NULL is returned.
 *
 * The returned pointer is valid until the next invocation.
 */
const char *maildir_walk(struct maildir *md);

/*
 * Move the message located at path inside src to dst.
 *
 * Returns zero on success, non-zero otherwise.
 */
int maildir_move(const struct maildir *src, const struct maildir *dst,
    const char *path);

/*
 * Parse the message located at path.
 *
 * The caller is responsible for freeing the returned memory using
 * message_free().
 */
struct message *message_parse(const char *path);

/*
 * Free message.
 */
void message_free(struct message *msg);

/*
 * Returns the message body if present.
 * Otherwise, NULL is returned.
 */
const char *message_get_body(const struct message *msg);

/*
 * Returns the corresponding value for the given header if present.
 * Otherwise, NULL is returned.
 */
const char *message_get_header(const struct message *msg, const char *header);

/*
 * Returns the message path.
 */
const char *message_get_path(const struct message *msg);

/*
 * Returns the last directory portion from the message path.
 */
const char *message_get_dirname(const struct message *msg);

/*
 * Allocate a new rule.
 *
 * The caller is responsible for freeing the returned memory using
 * rule_free().
 */
struct rule *rule_alloc(struct expr *ex);

/*
 * Free rule.
 */
void rule_free(struct rule *rl);

/*
 * Writes a human readable representation of the latest match to fh.
 */
void rule_inspect(const struct rule *rl, FILE *fh);

/*
 * Returns the destination path if the rule matches the given message.
 * Otherwise, NULL is returned.
 */
const char *rule_eval(struct rule *rl, const struct message *msg);

enum expr_type {
	EXPR_TYPE_AND,
	EXPR_TYPE_OR,
	EXPR_TYPE_NEG,
	EXPR_TYPE_BODY,
	EXPR_TYPE_HEADER,
	EXPR_TYPE_MOVE,
	EXPR_TYPE_NEW,
};

enum expr_pattern {
	EXPR_PATTERN_ICASE = 0x1,
};

/*
 * Allocate a new expression with the given type.
 *
 * The caller is responsible for associating the returned memory with a rule
 * using rule_alloc(). The rule will then take ownership of the memory and hence
 * free it at an appropriate time.
 */
struct expr *expr_alloc(enum expr_type type, struct expr *lhs,
    struct expr *rhs);

/*
 * Associate the given destination path with the expression.
 */
void expr_set_dest(struct expr *ex, char *dest);

/*
 * Associate the given headers with the expression.
 */
void expr_set_headers(struct expr *ex, struct expr_headers *headers);

/*
 * Associate the given pattern with the expression.
 * Returns zero if pattern is successfully compiled into a regular expression.
 * Otherwise, returns non-zero and if errstr is not NULL it will point to an
 * explanation on why the compilation failed.
 */
int expr_set_pattern(struct expr *ex, const char *pattern, int flags,
    const char **errstr);

/*
 * Allocate a list of headers.
 *
 * The caller is responsible for associating the returned memory with an
 * expression using expr_set_headers(). The expression will then take ownership
 * of the memory and hence free it at an appropriate time.
 */
struct expr_headers *expr_headers_alloc(void);

/*
 * Append the given header key to the list headers.
 */
void expr_headers_append(struct expr_headers *headers, char *key);

struct config {
	char *maildir;
	struct rule *rule;

	TAILQ_ENTRY(config) entry;
};

TAILQ_HEAD(config_list, config);

/*
 * Parses the configuration located at path and returns a config list on
 * success.
 * Otherwise, NULL is returned.
 */
struct config_list *parse_config(const char *path);

void log_debug(const char *fmt, ...)
	__attribute__((__format__ (printf, 1, 2)));

void log_info(const char *fmt, ...)
	__attribute__((__format__ (printf, 1, 2)));

extern const char *home, *hostname;
