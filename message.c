#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "extern.h"

#define FLAG(c)	(1 << ((c) - 'A'))

struct message {
	const char *path;
	const char *maildir;
	const char *body;
	char *buf;

	unsigned int flags;
	int nflags;

	struct header *headers;
	size_t nheaders;
};

struct header {
	const char *key;
	char *val;
	int decode;
};

static void message_parse_flags(struct message *);
static const char *message_parse_headers(struct message *);

static int cmpheader(const void *, const void *);
static char *decodeheader(const char *);
static int findheader(char *, char **, char **, char **, char **);

struct message *
message_parse(const char *path, const char *maildir)
{
	struct message *msg;
	ssize_t n;
	size_t msglen = 0;
	size_t msgsize = BUFSIZ;
	int fd;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd == -1) {
		warn("open: %s", path);
		return NULL;
	}

	msg = calloc(1, sizeof(*msg));
	if (msg == NULL)
		err(1, NULL);
	msg->path = path;
	msg->maildir = maildir;

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
			warn("read: %s", path);
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

	message_parse_flags(msg);

	return msg;
}

void
message_free(struct message *msg)
{
	size_t i;

	if (msg == NULL)
		return;

	for (i = 0; i < msg->nheaders; i++) {
		if (msg->headers[i].decode)
			free(msg->headers[i].val);
	}
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
	struct header *found;

	if (msg->nheaders == 0)
		return NULL;

	key.key = header;
	key.val = NULL;
	found = bsearch(&key, msg->headers, msg->nheaders,
	    sizeof(*msg->headers), cmpheader);
	if (found == NULL)
		return NULL;
	if (found->decode == 0) {
		found->val = decodeheader(found->val);
		found->decode = 1;
	}
	return found->val;
}

const char *
message_get_maildir(const struct message *msg)
{
	return msg->maildir;
}

const char *
message_get_subdir(const struct message *msg)
{
	static char buf[4];
	const char *beg, *end, *tmp;
	int buflen, len, n;

	beg = end = msg->path;
	for (;;) {
		tmp = strchr(end + 1, '/');
		if (tmp == NULL)
			break;
		beg = end + 1;
		end = tmp;
	}
	len = end - beg;
	if (len == 0) {
		warnx("%s: %s: could not find subdir", __func__, msg->path);
		return NULL;
	}
	buflen = sizeof(buf);
	n = snprintf(buf, buflen, "%.*s", len, beg);
	if (n == -1 || n >= buflen)
		errx(1, "%s: buffer too small", __func__);
	return buf;
}

const char *
message_get_path(const struct message *msg)
{
	return msg->path;
}

const char *
message_get_flags(const struct message *msg)
{
	static char buf[32];
	const char *p;
	unsigned int flags;
	unsigned int shift = 0;
	int i = 0;

	if (msg->nflags == 0) {
		/*
		 * Parsing of the flags could have failed, just give back the
		 * flags in its original form.
		 */
		p = strrchr(msg->path, ':');
		if (p == NULL)
			return "";
		return p;
	}

	buf[i++] = ':';
	buf[i++] = '2';
	buf[i++] = ',';
	for (flags = msg->flags; flags > 0; flags >>= 1) {
		if ((flags & 0x1))
			buf[i++] = 'A' + shift;
		shift++;
	}
	buf[i] = '\0';

	return buf;
}

void
message_set_flags(struct message *msg, unsigned char flag)
{
	assert(isupper(flag));

	if (msg->nflags == 0)
		return;

	msg->flags |= FLAG(flag);
}

static void
message_parse_flags(struct message *msg)
{
	const char *p;

	p = strrchr(msg->path, ':');
	if (p == NULL) {
		/* No flags is fine. */
		msg->nflags = 1;
		return;
	} else if (p[1] != '2' || p[2] != ',') {
		return;
	}

	for (p += 3; *p != '\0'; p++) {
		if (isupper(*p)) {
			msg->flags |= FLAG(*p);
		} else {
			log_debug("%s: %s: invalid flags", __func__, msg->path);
			return;
		}
	}
	msg->nflags = 1;
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
		msg->headers[msg->nheaders].decode = 0;
		msg->nheaders++;

		buf = valend + 1;
	}
	if (msg->nheaders > 0)
		qsort(msg->headers, msg->nheaders, sizeof(*msg->headers),
		    cmpheader);

	for (; *buf == '\n'; buf++)
		continue;
	return buf;
}

static int
cmpheader(const void *p1, const void *p2)
{
	const struct header *h1, *h2;

	h1 = p1;
	h2 = p2;
	return strcasecmp(h1->key, h2->key);
}

static char *
decodeheader(const char *str)
{
	const char *end;
	char *dec;
	size_t len;
	size_t i = 0;

	len = strlen(str);
	dec = malloc(len + 1);
	if (dec == NULL)
		err(1, NULL);
	for (;;) {
		if (*str == '\0')
			break;

		end = strchr(str, '\n');
		if (end == NULL)
			end = str + strlen(str);
		while (str != end)
			dec[i++] = *str++;

		for (; isspace(*str); str++)
			continue;
	}
	dec[i] = '\0';
	return dec;
}

static int
findheader(char *str, char **keybeg, char **keyend, char **valbeg,
    char **valend)
{
	size_t i;

	for (i = 0; str[i] != ':'; i++) {
		if (str[i] == '\0' || isspace(str[i]))
			return 1;
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
