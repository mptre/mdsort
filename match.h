#include "config.h"	/* HAVE_QUEUE */

#ifdef HAVE_QUEUE
#  include <sys/queue.h>
#else
#  include "compat-queue.h"
#endif

#include <limits.h>
#include <stddef.h>	/* size_t */

struct arena;
struct arena_scope;
struct environment;
struct macro_list;
struct maildir;

/* Return values for matches_exec(). */
enum {
	MATCH_EXEC_SUCCESS,
	MATCH_EXEC_REJECTED,
	MATCH_EXEC_ERROR,
};

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
int	matches_interpolate(struct match_list *, struct arena *);
int	matches_exec(const struct match_list *, struct maildir *,
    const struct environment *);
int	matches_inspect(const struct match_list *, const struct environment *,
    struct arena *);

struct match	*match_alloc(struct expr *, struct message *,
    struct arena_scope *);
void		 match_free(struct match *);

int		 match_interpolate(struct match *, const struct macro_list *,
    struct arena *);
struct match	*matches_find(struct match_list *, int);
int		 matches_remove(struct match_list *, int);
