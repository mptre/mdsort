#include "config.h"

#include "conf.h"
#include "environment.h"

int
main(void)
{
	struct config_list cl;
	struct environment env;
	int error;

	environment_init(&env);
	config_list_init(&cl);
	error = config_list_parse(&cl, "/dev/stdin", &env);
	config_list_free(&cl);
	return error;
}
