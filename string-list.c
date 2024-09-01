#include "string-list.h"

#include "config.h"

#include <err.h>
#include <stdlib.h>

/*
 * Allocate a list of strings.
 *
 * The caller is responsible for freeing the returned memory using
 * strings_free().
 */
struct string_list *
strings_alloc(void)
{
	struct string_list *strings;

	strings = malloc(sizeof(*strings));
	if (strings == NULL)
		err(1, NULL);
	TAILQ_INIT(strings);
	return strings;
}

void
strings_free(struct string_list *strings)
{
	struct string *str;

	if (strings == NULL)
		return;

	while ((str = TAILQ_FIRST(strings)) != NULL) {
		TAILQ_REMOVE(strings, str, entry);
		free(str);
	}
	free(strings);
}

size_t
strings_len(const struct string_list *strings)
{
	const struct string *str;
	size_t len = 0;

	TAILQ_FOREACH(str, strings, entry)
		len++;
	return len;
}

struct string *
strings_append(struct string_list *strings, const char *val)
{
	struct string *str;

	str = malloc(sizeof(*str));
	if (str == NULL)
		err(1, NULL);
	str->val = val;
	TAILQ_INSERT_TAIL(strings, str, entry);
	return str;
}
