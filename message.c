#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
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
	struct string_list *values;	/* list of all values for key */
};

static void message_parse_flags(struct message *);
static const char *message_parse_headers(struct message *);

static int cmpheader(const void *, const void *);
static char *decodeheader(const char *);
static int findheader(char *, char **, char **, char **, char **);
static ssize_t searchheader(const struct header *, size_t, const char *,
    size_t *);

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
		warn("open: %s", path);
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

	for (i = 0; i < msg->nheaders; i++)
		strings_free(msg->headers[i].values);
	free(msg->buf);
	free(msg->headers);
	free(msg);
}

const char *
message_get_body(const struct message *msg)
{
	return msg->body;
}

const struct string_list *
message_get_header(const struct message *msg, const char *header)
{
	struct header *hdr, *tmp;
	char *val;
	ssize_t idx;
	size_t i, nfound;

	idx = searchheader(msg->headers, msg->nheaders, header, &nfound);
	if (idx == -1)
		return NULL;

	hdr = msg->headers + idx;
	if (hdr->values == NULL) {
		hdr->values = strings_alloc();
		for (i = 0, tmp = hdr; i < nfound; i++, tmp++) {
			val = decodeheader(tmp->val);
			strings_append(hdr->values, val);
		}
	}
	return hdr->values;
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
	int bit = 0;
	int i = 0;

	if (msg->nflags == -1) {
		/*
		 * Parsing the flags failed, just give back the flags in its
		 * original form.
		 */
		p = strrchr(msg->path, ':');
		if (p == NULL)
			return "";
		return p;
	} else if (msg->nflags == 0) {
		return "";
	}

	buf[i++] = ':';
	buf[i++] = '2';
	buf[i++] = ',';
	for (flags = msg->flags; flags > 0; flags >>= 1) {
		if ((flags & 0x1))
			buf[i++] = 'A' + bit;
		bit++;
	}
	buf[i] = '\0';

	return buf;
}

int
message_has_flags(const struct message *msg, unsigned char flag)
{
	assert(isupper(flag));

	return msg->flags & FLAG(flag);
}

void
message_set_flags(struct message *msg, unsigned char flag, int add)
{
	assert(isupper(flag));

	if (msg->nflags == -1)
		return;

	if (add) {
		if (msg->flags & FLAG(flag))
			return;

		msg->flags |= FLAG(flag);
		msg->nflags++;
	} else {
		if ((msg->flags & FLAG(flag)) == 0)
			return;

		msg->flags &= ~FLAG(flag);
		msg->nflags--;
	}
}

static void
message_parse_flags(struct message *msg)
{
	const char *p;

	p = strrchr(msg->path, ':');
	if (p == NULL) {
		msg->nflags = 0;
		return;
	} else if (p[1] != '2' || p[2] != ',') {
		msg->nflags = -1;
		return;
	}

	for (p += 3; *p != '\0'; p++) {
		if (isupper(*p)) {
			message_set_flags(msg, *p, 1);
		} else {
			log_debug("%s: %s: invalid flags", __func__, msg->path);
			return;
		}
	}
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
		msg->headers[msg->nheaders].values = NULL;
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

static ssize_t
searchheader(const struct header *headers, size_t nmemb, const char *key,
    size_t *nfound)
{
	struct header needle;
	ssize_t hi, mi, lo;
	size_t i;
	int cmp;

	*nfound = 0;

	if (nmemb == 0)
		return -1;

	memset(&needle, 0, sizeof(needle));
	needle.key = key;
	lo = 0;
	hi = nmemb - 1;
	while (lo <= hi) {
		mi = lo + (hi - lo)/2;
		cmp = cmpheader(&needle, headers + mi);
		if (cmp == 0) {
			/* Move backwards to the first matching element. */
			for (; mi > 0; mi--)
				if (cmpheader(&needle, headers + mi - 1))
					break;
			for (i = mi; i < nmemb; i++)
				if (cmpheader(&needle, headers + i))
					break;
			*nfound = i - mi;
			return mi;
		}
		if (cmp > 0)
			lo = mi + 1;
		else
			hi = mi - 1;
	}
	return -1;
}
