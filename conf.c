#include "conf.h"
#include "config.h"
#include "libks/arena-vector.h"
#include "macro.h"

void
config_list_init(struct config_list *cl, struct arena_scope *s)
{
	cl->cl_macros = macros_alloc(MACRO_CTX_DEFAULT, s);
	ARENA_VECTOR_INIT(s, cl->cl_list, 8);
}

struct config *
config_list_append(struct config_list *cl)
{
	return ARENA_VECTOR_ALLOC(cl->cl_list);
}
