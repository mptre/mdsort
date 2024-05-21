#include "config.h"

#include "libks/fuzzer.h"

#include "conf.h"
#include "environment.h"

static void *
init(void)
{
	static struct environment env;

	environment_init(&env);
	return &env;
}
FUZZER_INIT(init);

static void
target(const char *path, void *userdata)
{
	struct config_list cl;
	const struct environment *env = userdata;

	config_list_init(&cl);
	config_list_parse(&cl, path, env);
	config_list_free(&cl);
}
FUZZER_TARGET_FILE(target);
