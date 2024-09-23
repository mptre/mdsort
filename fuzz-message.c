#include "config.h"

#include "libks/arena.h"
#include "libks/compiler.h"
#include "libks/fuzzer.h"

#include "message.h"

struct test_context {
	struct {
		struct arena	*eternal;
		struct arena	*scratch;
	} arena;
};

static void *
init(int UNUSED(argc), char **UNUSED(argv))
{
	static struct test_context c;

	c.arena.eternal = arena_alloc();
	c.arena.scratch = arena_alloc();
	return &c;
}
FUZZER_INIT(init);

static void
target(const char *path, void *userdata)
{
	struct test_context *c = userdata;
	struct message *msg;

	arena_scope(c->arena.eternal, eternal_scope);

	msg = message_parse("/dev", -1, path, &eternal_scope, c->arena.scratch);
	if (msg != NULL)
		message_get_body(msg);
	message_free(msg);
}
FUZZER_TARGET_FILE(target);

static void
teardown(void *userdata)
{
	struct test_context *c = userdata;

	arena_free(c->arena.eternal);
	arena_free(c->arena.scratch);
}
FUZZER_TEARDOWN(teardown);
