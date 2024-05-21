#include "config.h"

#include "libks/compiler.h"
#include "libks/fuzzer.h"

#include "message.h"

static void
target(const char *path, void *UNUSED(userdata))
{
	struct message *msg;

	msg = message_parse("/dev", -1, path);
	if (msg != NULL)
		message_get_body(msg);
	message_free(msg);
}
FUZZER_TARGET_FILE(target);
