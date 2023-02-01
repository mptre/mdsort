#include "macro.h"

#include <string.h>
#include <stdlib.h>
#include <err.h>

struct macro_list *
macros_alloc(unsigned int ctx)
{
	struct macro_list *macros;

	macros = calloc(1, sizeof(*macros));
	if (macros == NULL)
		err(1, NULL);
	macros->ml_size = sizeof(macros->ml_v) / sizeof(macros->ml_v[0]);
	macros->ml_ctx = ctx;
	TAILQ_INIT(&macros->ml_list);
	return macros;
}

void
macros_free(struct macro_list *macros)
{
	struct macro *mc;

	if (macros == NULL)
		return;

	while ((mc = TAILQ_FIRST(&macros->ml_list)) != NULL) {
		TAILQ_REMOVE(&macros->ml_list, mc, mc_entry);
		if ((mc->mc_flags & MACRO_FLAG_CONST) == 0) {
			free(mc->mc_name);
			free(mc->mc_value);
		}
		if ((mc->mc_flags & MACRO_FLAG_STATIC) == 0)
			free(mc);
	}
	free(macros);
}

enum macro_error
macros_insert(struct macro_list *macros, char *name, char *value,
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

	if (macros->ml_nmemb < macros->ml_size) {
		mc = &macros->ml_v[macros->ml_nmemb++];
		flags |= MACRO_FLAG_STATIC;
	} else {
		mc = malloc(sizeof(*mc));
		if (mc == NULL)
			err(1, NULL);
	}

	mc->mc_name = name;
	mc->mc_value = value;
	mc->mc_refs = 0;
	mc->mc_defs = 0;
	mc->mc_lno = lno;
	mc->mc_flags = flags;
	TAILQ_INSERT_TAIL(&macros->ml_list, mc, mc_entry);
	return MACRO_ERR_NONE;
}

/*
 * Insert a constant macro, only used for non-default contexts.
 * Note, the macro list does not claim memory ownership of the given name and
 * value thus the caller must ensure that the lifetime of the two aforementioned
 * pointers supersedes the macro list.
 */
void
macros_insertc(struct macro_list *macros, const char *name, const char *value)
{
	enum macro_error error;

	/*
	 * A macro list used in a non-default context is always stack allocated.
	 * Therefore ensure that the stack storage is sufficient.
	 */
	if (macros->ml_nmemb == macros->ml_size)
		errx(1, "%s: stack storage exhausted", __func__);

	/* Dangerous casting ahead but a macro is never mutated. */
	error = macros_insert(macros, (char *)name, (char *)value,
	    MACRO_FLAG_CONST, 0);
	if (error)
		errx(1, "%s: error %u", __func__, error);
}

struct macro *
macros_find(const struct macro_list *macros, const char *name)
{
	struct macro *mc;

	TAILQ_FOREACH(mc, &macros->ml_list, mc_entry) {
		if (strcmp(mc->mc_name, name) == 0)
			return mc;
	}
	return NULL;
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
ismacro(const char *str, char **macro)
{
	size_t i;

	if (str[0] != '$' || str[1] != '{')
		return 0;

	for (i = 2; str[i] != '}'; i++) {
		if (str[i] == '\0')
			return -1;
	}

	*macro = strndup(&str[2], i - 2);
	if (*macro == NULL)
		err(1, NULL);
	return (ssize_t)(i + 1);
}
