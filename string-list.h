#include "config.h"	/* HAVE_QUEUE */

#ifdef HAVE_QUEUE
#  include <sys/queue.h>
#else
#  include "compat-queue.h"
#endif

#include <stddef.h>	/* size_t */

struct string {
	char			*val;
	TAILQ_ENTRY(string)	 entry;
};

TAILQ_HEAD(string_list, string);

struct string_list	*strings_alloc(void);
void			 strings_free(struct string_list *);

struct string	*strings_append(struct string_list *, char *);

size_t	strings_len(const struct string_list *);
