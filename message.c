#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#include "extern.h"

struct message {
	const char *path;
	const char *body;
	char *buf;

	struct header *headers;
	size_t nheaders;
};

struct header {
	const char *key;
	const char *val;
};

static const char *message_parse_headers(struct message *);

static int cmpheader(const void *, const void *);
static int findheader(char *, char **, char **, char **, char **);

struct message *
message_parse(const char *path)
{
	struct message *msg;
	ssize_t n;
	size_t msglen = 0;
	size_t msgsize = BUFSIZ;
	int fd;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd == -1) {
		warn("%s", path);
		return NULL;
	}

	msg = calloc(1, sizeof(*msg));
	if (msg == NULL)
		err(1, NULL);
	msg->path = path;

	for (;;) {
		if (msglen >= msgsize - 1 || msg->buf == NULL) {
			msg->buf = reallocarray(msg->buf, 2, msgsize);
			if (msg->buf == NULL)
				err(1, NULL);
			msgsize *= 2;
		}

		n = read(fd, msg->buf + msglen, msgsize - msglen - 1);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			warn("%s", path);
			close(fd);
			message_free(msg);
			return NULL;
		} else if (n == 0) {
			break;
		}
		msglen += n;
	}
	assert(msglen < msgsize);
	msg->buf[msglen] = '\0';
	close(fd);

	msg->body = message_parse_headers(msg);

	return msg;
}

void
message_free(struct message *msg)
{
	if (msg == NULL)
		return;
	free(msg->buf);
	free(msg->headers);
	free(msg);
}

const char *
message_get_body(const struct message *msg)
{
	return msg->body;
}

const char *
message_get_header(const struct message *msg, const char *header)
{
	struct header key;
	const struct header *found;

	key.key = header;
	key.val = NULL;
	found = bsearch(&key, msg->headers, msg->nheaders,
	    sizeof(*msg->headers), cmpheader);
	if (found == NULL)
		return NULL;
	return found->val;
}

const char *
message_get_path(const struct message *msg)
{
	return msg->path;
}

static const char *
message_parse_headers(struct message *msg)
{
	char *buf, *keybeg, *keyend, *valbeg, *valend;

	buf = msg->buf;
	while (findheader(buf, &keybeg, &keyend, &valbeg, &valend) == 0) {
		*keyend = '\0';
		*valend = '\0';

		msg->headers = reallocarray(msg->headers, msg->nheaders + 1,
		    sizeof(*msg->headers));
		if (msg->headers == NULL)
			err(1, NULL);
		msg->headers[msg->nheaders].key = keybeg;
		msg->headers[msg->nheaders].val = valbeg;
		msg->nheaders++;

		buf = valend + 1;
	}
	qsort(msg->headers, msg->nheaders, sizeof(*msg->headers), cmpheader);
	if (buf[0] == '\0' || buf[1] == '\0')
		return NULL;
	return buf + 1;
}

static int
cmpheader(const void *p1, const void *p2)
{
	const struct header *h1, *h2;

	h1 = p1;
	h2 = p2;
	return strcasecmp(h1->key, h2->key);
}

static int
findheader(char *str, char **keybeg, char **keyend, char **valbeg,
    char **valend)
{
	size_t i = 0;

	for (;;) {
		if (str[i] == '\n')
			return 1;
		if (str[i] == '\0' || isspace(str[i]))
			return 1;
		if (str[i] == ':')
			break;
		i++;
	}
	*keybeg = str;
	*keyend = str + i;

	/* Skip leading whitespace in value. */
	for (i++; str[i] == ' ' || str[i] == '\t'; i++)
		continue;
	*valbeg = str + i;

	for (;; i++) {
		if (str[i] == '\0')
			return 1;
		if (str[i] != '\n')
			continue;

		if (str[i + 1] == ' ' || str[i + 1] == '\t')
			continue;
		break;
	}
	*valend = str + i;

	return 0;
}
