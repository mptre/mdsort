#ifdef HAVE_QUEUE
#  include <sys/queue.h>
#else
#  include "compat-queue.h"
#endif

#include <limits.h>
#include <stddef.h> /* size_t */

/*
 * match ---------------------------------------------------------------------
 */

struct environment;
struct macro_list;
struct maildir;

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

int		 match_interpolate(struct match *, const struct macro_list *);
struct match	*matches_find(struct match_list *, int);
int		 matches_remove(struct match_list *, int);

/*
 * time ------------------------------------------------------------------------
 */

char	*time_format(long long int, char *, size_t);
int	 time_parse(const char *, long long int *, const struct environment *);

/*
 * string ----------------------------------------------------------------------
 */

struct string {
	char			*val;
	TAILQ_ENTRY(string)	 entry;
};

TAILQ_HEAD(string_list, string);

struct string_list	*strings_alloc(void);
void			 strings_free(struct string_list *);

struct string	*strings_append(struct string_list *, char *);

size_t	strings_len(const struct string_list *);

/*
 * util ------------------------------------------------------------------------
 */

int	exec(char *const *, int);

char	*pathjoin(char *, size_t, const char *, const char *);
char	*pathslice(const char *, char *, size_t, int, int);

size_t	nspaces(const char *);

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

void	logit(const char *, ...)
	__attribute__((__format__(printf, 1, 2)));
