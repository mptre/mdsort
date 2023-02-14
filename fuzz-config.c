#include "config.h"

#include <string.h>

#include "conf.h"
#include "extern.h"

int
main(void)
{
	struct config_list cl;
	struct environment env;
	int error;

	memset(&env, 0, sizeof(env));
	config_init(&cl);
	error = config_parse(&cl, "/dev/stdin", &env);
	config_free(&cl);
	return error;
}
