#include "config.h"

#ifdef HAVE_QUEUE
#  include <sys/queue.h>
#else
#  include "compat-queue.h"
#endif

#include <sys/types.h>	/* size_t, ssize_t */

#define MACRO_CTX_DEFAULT	0x00000001u
#define MACRO_CTX_ACTION	0x00000002u

enum macro_error {
	MACRO_ERR_NONE,
	MACRO_ERR_CTX,
	MACRO_ERR_EXIST,
	MACRO_ERR_STICKY,
};

struct macro {
	char			*mc_name;
	char			*mc_value;
	unsigned int		 mc_refs;
	unsigned int		 mc_defs;
	unsigned int		 mc_lno;
	unsigned int		 mc_flags;
#define MACRO_FLAG_STATIC	0x00000001u	/* storage provided by ml_v */
#define MACRO_FLAG_CONST	0x00000002u	/* may not be freed */
#define MACRO_FLAG_STICKY	0x00000004u	/* may not be overwritten */

	TAILQ_ENTRY(macro)	 mc_entry;
};

struct macro_list {
	struct macro		ml_v[2];
	size_t			ml_nmemb;
	size_t			ml_size;
	unsigned int		ml_ctx;

	TAILQ_HEAD(, macro)	ml_list;
};

struct macro_list	*macros_alloc(unsigned int);
void			 macros_free(struct macro_list *);
enum macro_error	 macros_insert(struct macro_list *, char *, char *,
    unsigned int, unsigned int);
void			 macros_insertc(struct macro_list *, const char *,
    const char *);
struct macro		*macros_find(const struct macro_list *, const char *);
unsigned int		 macro_context(const char *);

ssize_t	ismacro(const char *, char **);
