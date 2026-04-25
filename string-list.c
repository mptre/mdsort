#include "string-list.h"
#include "config.h"
#include "libks/arena.h"
#include "libks/list.h"

struct string_list_impl {
	struct {
		struct arena_scope	*eternal_scope;
	} arena;
};

union string_list_addr {
	struct string_list_impl	*impl;
	struct string_list	*list;
};

static struct string_list_impl *
list_to_impl(struct string_list *list)
{
	union string_list_addr u = {.list = list};

	u.impl--;
	return u.impl;
}

static struct string_list *
impl_to_list(struct string_list_impl *impl)
{
	union string_list_addr u = {.impl = impl};

	u.impl++;
	return u.list;
}

struct string_list *
strings_alloc(struct arena_scope *eternal_scope)
{
	struct string_list_impl *impl;
	struct string_list *strings;

	impl = arena_malloc(eternal_scope, sizeof(*impl) + sizeof(*strings));
	impl->arena.eternal_scope = eternal_scope;
	strings = impl_to_list(impl);
	LIST_INIT(strings);
	return strings;
}

size_t
strings_len(const struct string_list *strings)
{
	const struct string *str;
	size_t len = 0;

	LIST_FOREACH(str, strings)
		len++;
	return len;
}

struct string *
strings_append(struct string_list *strings, const char *val)
{
	struct string_list_impl *impl = list_to_impl(strings);
	struct string *str;

	str = arena_malloc(impl->arena.eternal_scope, sizeof(*str));
	str->val = val;
	LIST_INSERT_TAIL(strings, str);
	return str;
}
