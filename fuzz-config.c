#include "config.h"

#include <string.h>

#include "extern.h"

int
main(void)
{
	struct config_list config;
	struct environment env;
	int error;

	memset(&env, 0, sizeof(env));
	config_init(&config);
	error = config_parse(&config, "/dev/stdin", &env);
	config_free(&config);
	return error;
}
