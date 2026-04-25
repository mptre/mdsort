#include "macro.h"
#include "config.h"
#include <err.h>
#include <string.h>
#include "libks/arena-vector.h"
#include "libks/arena.h"
#include "libks/vector.h"

struct macro {
	const char	*mc_name;
	const char	*mc_value;
	unsigned int	 mc_refs;
	unsigned int	 mc_defs;
	unsigned int	 mc_lno;
	unsigned int	 mc_flags;
};

struct macro_list {
	unsigned int		ml_ctx;
	VECTOR(struct macro)	ml_list;
};

struct macro_list *
macros_alloc(unsigned int ctx, struct arena_scope *s)
{
	struct macro_list *macros;

	macros = arena_calloc(s, 1, sizeof(*macros));
	macros->ml_ctx = ctx;
	ARENA_VECTOR_INIT(s, macros->ml_list, 4);
	return macros;
}

enum macro_error
macros_insert(struct macro_list *macros, const char *name, const char *value,
    unsigned int flags, unsigned int lno)
{
	struct macro *mc;

	if ((macro_context(name) & macros->ml_ctx) == 0)
		return MACRO_ERR_CTX;
	if ((mc = macros_find(macros, name)) != NULL) {
		if (mc->mc_flags & MACRO_FLAG_STICKY) {
			if ((flags & MACRO_FLAG_STICKY) || ++mc->mc_defs > 1)
				return MACRO_ERR_EXIST;
			return MACRO_ERR_STICKY;
		}
		return MACRO_ERR_EXIST;
	}

	mc = ARENA_VECTOR_CALLOC(macros->ml_list);
	mc->mc_name = name;
	mc->mc_value = value;
	mc->mc_refs = 0;
	mc->mc_defs = 0;
	mc->mc_lno = lno;
	mc->mc_flags = flags;
	return MACRO_ERR_NONE;
}

/*
 * Insert a constant macro, only used for non-default contexts.
 */
void
macros_insertc(struct macro_list *macros, const char *name, const char *value)
{
	enum macro_error error;

	error = macros_insert(macros, name, value, 0, 0);
	if (error)
		errx(1, "%s: error %u", __func__, error);
}

struct macro *
macros_find(const struct macro_list *macros, const char *name)
{
	size_t i;

	for (i = 0; i < VECTOR_LENGTH(macros->ml_list); i++) {
		struct macro *mc = &macros->ml_list[i];

		if (strcmp(mc->mc_name, name) == 0)
			return mc;
	}
	return NULL;
}

struct macro **
macros_unused(const struct macro_list *macros, struct arena_scope *s)
{
	VECTOR(struct macro *) unused;
	size_t n = VECTOR_LENGTH(macros->ml_list);
	size_t i;

	ARENA_VECTOR_INIT(s, unused, n);

	for (i = 0; i < n; i++) {
		struct macro *mc = &macros->ml_list[i];

		if (mc->mc_refs > 0)
			continue;
		*ARENA_VECTOR_ALLOC(unused) = mc;
	}
	return unused;
}

void
macro_ref(struct macro *mc)
{
	mc->mc_refs++;
}

const char *
macro_get_name(const struct macro *mc)
{
	return mc->mc_name;
}

const char *
macro_get_value(const struct macro *mc)
{
	return mc->mc_value;
}

unsigned int
macro_get_lno(const struct macro *mc)
{
	return mc->mc_lno;
}

unsigned int
macro_context(const char *name)
{
	static struct {
		const char	*name;
		unsigned int	 ctx;
	} macros[] = {
		{ "path",	MACRO_CTX_ACTION },

		{ NULL,	MACRO_CTX_DEFAULT },
	};
	int i;

	for (i = 0; macros[i].name != NULL; i++) {
		if (strcmp(macros[i].name, name) == 0)
			return macros[i].ctx;
	}
	return MACRO_CTX_DEFAULT;
}

/*
 * Determine if the given string starts with a macro. Returns one of the
 * following:
 *
 *     >0    The length of the found macro. The caller is responsible for
 *           freeing the memory pointed to in name.
 *      0    Macro not found.
 *     -1    Unterminated macro found.
 */
ssize_t
ismacro(const char *str, char **macro, struct arena_scope *s)
{
	size_t i;

	if (str[0] != '$' || str[1] != '{')
		return 0;

	for (i = 2; str[i] != '}'; i++) {
		if (str[i] == '\0')
			return -1;
	}

	*macro = arena_strndup(s, &str[2], i - 2);
	return (ssize_t)(i + 1);
}
