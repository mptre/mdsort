#include <sys/types.h>	/* ssize_t */

struct arena_scope;

#define MACRO_CTX_DEFAULT	0x00000001u
#define MACRO_CTX_ACTION	0x00000002u

#define MACRO_FLAG_STICKY	0x00000001u	/* may not be overwritten */

enum macro_error {
	MACRO_ERR_NONE,
	MACRO_ERR_CTX,
	MACRO_ERR_EXIST,
	MACRO_ERR_STICKY,
};

struct macro_list	 *macros_alloc(unsigned int, struct arena_scope *);
enum macro_error	  macros_insert(struct macro_list *, const char *,
    const char *, unsigned int, unsigned int);
void			  macros_insertc(struct macro_list *, const char *,
    const char *);
struct macro		 *macros_find(const struct macro_list *, const char *);
struct macro		**macros_unused(const struct macro_list *,
    struct arena_scope *);

void	macro_ref(struct macro *);

const char	*macro_get_name(const struct macro *);
const char	*macro_get_value(const struct macro *);
unsigned int	 macro_get_lno(const struct macro *);

unsigned int	macro_context(const char *);

ssize_t	ismacro(const char *, char **, struct arena_scope *);
