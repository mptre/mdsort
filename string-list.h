#include <stddef.h>	/* size_t */

#include "libks/list.h"

LIST(string_list, string);

struct string {
	const char		*val;
	LIST_ENTRY(string_list, string);
};

struct string_list	*strings_alloc(void);
void			 strings_free(struct string_list *);

struct string	*strings_append(struct string_list *, const char *);

size_t	strings_len(const struct string_list *);
