#ifdef HAVE_QUEUE
#  include <sys/queue.h>
#else
#  include "compat-queue.h"
#endif

#include <stddef.h> /* size_t */

/*
 * time ------------------------------------------------------------------------
 */

struct environment;

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
