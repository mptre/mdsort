#include "config.h"

#include "message.h"

int
main(void)
{
	struct message *msg;
	const char *body = NULL;

	msg = message_parse("/dev", -1, "/dev/stdin");
	if (msg != NULL)
		body = message_get_body(msg);
	message_free(msg);
	return body != NULL ? 0 : 1;
}
