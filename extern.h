/*
 * Forwards declarations.
 */
struct expr;
struct expr_headers;
struct rule_match;

/*
 * Open the maildir directory located at path.
 * The nowalk argument must be 0 in order to traverse all subdirectories in the
 * maildir.
 *
 * The caller is responsible for freeing the returned memory using
 * maildir_close().
 */
struct maildir *maildir_open(const char *path, int nowalk);

/*
 * Open the subdirectory as given by the maildir but rooted at path, which must
 * also be a valid maildir.
 * The match is used to interpolate any back-references in path.
 *
 * The caller is responsible for freeing the returned memory using
 * maildir_close().
 */
struct maildir *maildir_openat(const struct maildir *md, const char *path,
	const struct rule_match *match);

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
 * Returns the maildir path.
 */
const char *maildir_get_path(const struct maildir *md);

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

enum rule_type {
	RULE_TYPE_AND = 1,
	RULE_TYPE_OR,
};

/* XXX */
struct rule *rule_alloc(void);

/* XXX */
void rule_free(struct rule *rl);

/* XXX */
void rule_add_expr(struct rule *rl, struct expr *ex);

/* XXX */
int rule_set_type(struct rule *rl, enum rule_type type);

/*
 * Set the destination maildir to the given path.
 */
void rule_set_dest(struct rule *rl, const char *path);

/*
 * Get the maildir destination path.
 */
const char *rule_get_dest(const struct rule *rl);

/*
 * Returns the substrings in the given message that matched the first header
 * expression in rule.
 * If none of the expressions in rule matched the message, NULL is returned.
 */
const struct rule_match *rule_eval(struct rule *rl, const struct message *msg);

/*
 * Returns the nth match if present.
 * Otherwise, NULL is returned.
 */
const char *rule_match_get(const struct rule_match *match, unsigned long n);

/*
 * Returns a human readable representation of the match.
 *
 * The caller is responsible for freeing the returned memory using free().
 */
char *rule_match_str(const struct rule_match *match);

enum expr_type {
	EXPR_TYPE_BODY = 1,
	EXPR_TYPE_HEADER,
	EXPR_TYPE_NEW,
};

enum expr_pattern {
	EXPR_PATTERN_ICASE = 0x1,
};

/* XXX */
struct expr *expr_alloc(enum expr_type);

/*
 * Associate the given headers with the expression.
 */
void expr_set_headers(struct expr *ex, struct expr_headers *headers);

/* XXX */
void expr_set_negate(struct expr *ex, int negate);

/* XXX */
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
void expr_headers_append(struct expr_headers *headers, const char *key);

struct config_list {
	struct config *list;
	size_t nmemb;
};

struct config {
	char *maildir;
	struct rule **rules;
	size_t nrules;
};

/* XXX */
const struct config_list *parse_config(const char *path);

/* XXX */
void log_debug(const char *fmt, ...)
	__attribute__((__format__ (printf, 1, 2)));

/* XXX */
void log_info(const char *fmt, ...)
	__attribute__((__format__ (printf, 1, 2)));

extern const char *home, *hostname;
