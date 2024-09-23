#include "config.h"

#include "libks/arena.h"
#include "libks/compiler.h"
#include "libks/fuzzer.h"

#include "conf.h"
#include "environment.h"

struct test_context {
	struct environment       env;
	struct arena            *scratch;
};

static void *
init(int UNUSED(argc), char **UNUSED(argv))
{
	static struct test_context c;

	environment_init(&c.env);
	c.scratch = arena_alloc();
	return &c;
}
FUZZER_INIT(init);

static void
target(const char *path, void *userdata)
{
	struct config_list cl;
	struct test_context *c = userdata;

	arena_scope(c->scratch, s);

	config_list_init(&cl, &s);
	config_list_parse(&cl, path, &c->env, &s);
}
FUZZER_TARGET_FILE(target);

static void
teardown(void *userdata)
{
	struct test_context *c = userdata;

	arena_free(c->scratch);
}
FUZZER_TEARDOWN(teardown);
