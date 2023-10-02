#include "conf.h"

#include "config.h"

#include <err.h>

#include "libks/vector.h"

#include "expr.h"
#include "extern.h"
#include "macro.h"

void
config_init(struct config_list *cl)
{
	cl->cl_macros = macros_alloc(MACRO_CTX_DEFAULT);
	if (VECTOR_INIT(cl->cl_list))
		err(1, NULL);
}

void
config_free(struct config_list *cl)
{
	if (cl == NULL)
		return;

	macros_free(cl->cl_macros);

	while (!VECTOR_EMPTY(cl->cl_list)) {
		struct config *conf;

		conf = VECTOR_POP(cl->cl_list);
		strings_free(conf->paths);
		expr_free(conf->expr);
	}
	VECTOR_FREE(cl->cl_list);
}
