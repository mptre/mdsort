#include "config.h"

#include <netinet/in.h>
#include <resolv.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "extern.h"

struct header {
	unsigned int id;

	unsigned int flags;
#define HEADER_FLAG_DIRTY	0x00000001	/* val must be freed */

	const char *key;
	char *val;
	struct string_list *values;	/* list of all values for key */
};

static int message_flags_parse(struct message_flags *, const char *);
static int message_flags_resolve(unsigned char, unsigned int *, unsigned int *);
static void message_headers_realloc(struct message *);
static int message_is_content_type(const struct message *, const char *);
static const char *message_parse_headers(struct message *);

static int cmpheaderid(const void *, const void *);
static int cmpheaderkey(const void *, const void *);
static char *decodeheader(const char *);
static int findheader(char *, char **, char **, char **, char **);
static ssize_t searchheader(const struct header *, size_t, const char *,
    size_t *);

static int parseattachments(const struct message *, struct message_list *,
    int);
static const char *findboundary(const char *, const char *, int *);
static int parseboundary(const char *, char **);

static char *b64decode(const char *);
static const char *skipline(const char *);
static ssize_t strflags(unsigned int, unsigned char, char *, size_t);
static int strword(const char *, const char *);

char *
message_flags_str(const struct message_flags *flags, char *buf, size_t bufsiz)
{
	ssize_t n;
	size_t i = 0;

	/* Ensure room for at least the empty set of flags ":2,\0". */
	if (bufsiz < 4)
		goto fail;

	buf[i++] = ':';
	buf[i++] = '2';
	buf[i++] = ',';
	n = strflags(flags->mf_flags[0], 'A', &buf[i], bufsiz - i);
	if (n == -1)
		goto fail;
	i += n;
	n = strflags(flags->mf_flags[1], 'a', &buf[i], bufsiz - i);
	if (n == -1)
		goto fail;
	i += n;
	buf[i] = '\0';

	return buf;

fail:
	warnc(ENAMETOOLONG, "%s", __func__);
	return NULL;
}

int
message_flags_isset(const struct message_flags *flags, unsigned char flag)
{
	unsigned int idx, mask;

	if (message_flags_resolve(flag, &idx, &mask))
		return 0;
	if (flags->mf_flags[idx] & mask)
		return 1;
	return 0;
}

int
message_flags_set(struct message_flags *flags, unsigned char flag, int add)
{
	unsigned int idx, mask;

	if (message_flags_resolve(flag, &idx, &mask))
		return 1;

	if (add)
		flags->mf_flags[idx] |= mask;
	else
		flags->mf_flags[idx] &= ~mask;
	return 0;
}

/*
 * Parse the message located at path.
 *
 * The caller is responsible for freeing the returned memory using
 * message_free().
 */
struct message *
message_parse(const char *dir, int dirfd, const char *path)
{
	struct message *msg;
	ssize_t n;
	size_t msglen = 0;
	size_t msgsize = BUFSIZ;
	int fd = -1;

	fd = openat(dirfd, path, O_RDONLY | O_CLOEXEC);
	if (fd == -1) {
		warn("open: %s/%s", dir, path);
		return NULL;
	}

	msg = calloc(1, sizeof(*msg));
	if (msg == NULL)
		err(1, NULL);
	msg->me_buf = malloc(msgsize);
	if (msg->me_buf == NULL)
		err(1, NULL);
	msg->me_path = pathjoin(msg->me_pbuf, sizeof(msg->me_pbuf), dir,
	    path);
	if (msg->me_path == NULL) {
		warnc(ENAMETOOLONG, "%s", __func__);
		goto err;
	}

	for (;;) {
		if (msglen >= msgsize - 1) {
			msg->me_buf = reallocarray(msg->me_buf, 2, msgsize);
			if (msg->me_buf == NULL)
				err(1, NULL);
			msgsize *= 2;
		}

		n = read(fd, msg->me_buf + msglen, msgsize - msglen - 1);
		if (n == -1) {
			warn("read: %s", path);
			goto err;
		} else if (n == 0) {
			break;
		}
		msglen += n;
	}
	assert(msglen < msgsize);
	msg->me_buf[msglen] = '\0';
	close(fd);
	fd = -1;

	msg->me_body = message_parse_headers(msg);

	if (message_flags_parse(&msg->me_flags, msg->me_path))
		goto err;

	return msg;

err:
	if (fd != -1)
		close(fd);
	message_free(msg);
	return NULL;
}

void
message_free(struct message *msg)
{
	size_t i;

	if (msg == NULL)
		return;

	for (i = 0; i < msg->me_headers.h_nmemb; i++) {
		strings_free(msg->me_headers.h_v[i].values);
		if (msg->me_headers.h_v[i].flags & HEADER_FLAG_DIRTY)
			free(msg->me_headers.h_v[i].val);
	}

	free(msg->me_buf);
	free(msg->me_buf_dec);
	free(msg->me_headers.h_v);
	free(msg);
}

int
message_writeat(struct message *msg, int dirfd, const char *path)
{
	const struct header *hdr;
	FILE *fh;
	unsigned int i;
	int fd;
	int error = 0;

	fd = openat(dirfd, path, O_WRONLY | O_CLOEXEC);
	if (fd == -1) {
		warn("open: %s", path);
		return 1;
	}
	fh = fdopen(fd, "we");
	if (fh == NULL) {
		warn("fdopen: %s", path);
		close(fd);
		return 1;
	}

	/* Preserve ordering of headers. */
	if (msg->me_headers.h_nmemb > 0)
		qsort(msg->me_headers.h_v, msg->me_headers.h_nmemb,
		    sizeof(*msg->me_headers.h_v), cmpheaderid);

	for (i = 0; i < msg->me_headers.h_nmemb; i++) {
		hdr = &msg->me_headers.h_v[i];
		if (fprintf(fh, "%s: %s\n", hdr->key, hdr->val) < 0) {
			warn("fprintf");
			error = 1;
			goto out;
		}
	}

	if (fprintf(fh, "\n%s", msg->me_body) < 0) {
		warn("fprintf");
		error = 1;
	}

out:
	fclose(fh);
	close(fd);
	return error;
}

/*
 * Get the message body. If the message contains alternative representations,
 * text is favored over HTML.
 * Returns a non-NULL pointer on success, NULL otherwise.
 */
const char *
message_get_body(struct message *msg)
{
	struct message_list *attachments;
	const struct message *attach;
	const struct message *found = NULL;
	const char *encoding;

	if (msg->me_buf_dec != NULL)
		return msg->me_buf_dec;
	if (!message_is_content_type(msg, "multipart/alternative"))
		return msg->me_body;

	/* Attachment parsing errors are considered fatal. */
	if (message_get_attachments(msg, &attachments))
		return NULL;

	/* Scan attachments, favor plain text over HTML. */
	TAILQ_FOREACH(attach, attachments, me_entry) {
		if (message_is_content_type(attach, "text/plain")) {
			found = attach;
			break;
		} else if (message_is_content_type(attach, "text/html")) {
			if (found == NULL)
				found = attach;
		}
	}
	if (found == NULL) {
		message_list_free(attachments);
		return msg->me_body;
	}

	/*
	 * Attachments are resolved recursively above, it's therefore fine to
	 * access the body of the attachment directly.
	 */
	encoding = message_get_header1(found, "Content-Transfer-Encoding");
	if (encoding == NULL || strcmp(encoding, "base64")) {
		msg->me_buf_dec = strdup(found->me_body);
		if (msg->me_buf_dec == NULL)
			err(1, NULL);
	} else {
		msg->me_buf_dec = b64decode(found->me_body);
		if (msg->me_buf_dec == NULL)
			warnx("%s: failed to decode body", msg->me_path);
	}
	message_list_free(attachments);

	return msg->me_buf_dec;
}

const struct string_list *
message_get_header(const struct message *msg, const char *header)
{
	struct header *hdr, *tmp;
	char *val;
	ssize_t idx;
	size_t i, nfound;

	idx = searchheader(msg->me_headers.h_v, msg->me_headers.h_nmemb,
	    header, &nfound);
	if (idx == -1)
		return NULL;

	hdr = msg->me_headers.h_v + idx;
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
message_get_header1(const struct message *msg, const char *header)
{
	const struct string_list *values;
	const struct string *str;

	values = message_get_header(msg, header);
	if (values == NULL)
		return NULL;
	str = TAILQ_FIRST(values);
	return str->val;
}

void
message_set_header(struct message *msg, const char *header, char *val)
{
	struct header *hdr;
	ssize_t idx;
	size_t nfound, tail;

	idx = searchheader(msg->me_headers.h_v, msg->me_headers.h_nmemb,
	    header, &nfound);
	if (idx == -1) {
		message_headers_realloc(msg);

		hdr = &msg->me_headers.h_v[msg->me_headers.h_nmemb];
		hdr->flags = HEADER_FLAG_DIRTY;
		hdr->id = msg->me_headers.h_nmemb;
		hdr->key = header;
		hdr->val = val;
		hdr->values = NULL;
		msg->me_headers.h_nmemb++;

		qsort(msg->me_headers.h_v, msg->me_headers.h_nmemb,
		    sizeof(*msg->me_headers.h_v), cmpheaderkey);
	} else {
		if (nfound > 1) {
			/*
			 * Multiple occurrences of the given header.
			 * Remove all occurrences except the first one.
			 */
			tail = msg->me_headers.h_nmemb - (idx + nfound);
			memmove(&msg->me_headers.h_v[idx + 1],
			    &msg->me_headers.h_v[idx + nfound],
			    tail * sizeof(*msg->me_headers.h_v));
			msg->me_headers.h_nmemb -= nfound - 1;
		}

		hdr = &msg->me_headers.h_v[idx];
		if (hdr->flags & HEADER_FLAG_DIRTY)
			free(hdr->val);
		else
			hdr->flags |= HEADER_FLAG_DIRTY;
		hdr->val = val;
		strings_free(hdr->values);
		hdr->values = NULL;
	}
}

/*
 * Returns non-zero if label is present in the X-Label header, with respect
 * to word boundaries.
 */
int
message_has_label(const struct message *msg, const char *label)
{
	const struct string_list *labels;
	const struct string *str;

	labels = message_get_header(msg, "X-Label");
	if (labels == NULL)
		return 0;

	TAILQ_FOREACH(str, labels, entry) {
		if (strword(str->val, label))
			return 1;
	}

	return 0;
}

/*
 * Parse all attachments in message into the given message list.
 * Returns zero on success, even if no attachments where found.
 * The caller is responsible for freeing the message list using
 * message_list_free().
 *
 * Otherwise, non-zero is returned.
 */
int
message_get_attachments(const struct message *msg,
    struct message_list **attachments)
{
	*attachments = malloc(sizeof(**attachments));
	if (*attachments == NULL)
		err(1, NULL);
	TAILQ_INIT(*attachments);

	if (parseattachments(msg, *attachments, 0)) {
		message_list_free(*attachments);
		return 1;
	}

	return 0;
}

void
message_list_free(struct message_list *messages)
{
	struct message *msg;

	if (messages == NULL)
		return;

	while ((msg = TAILQ_FIRST(messages)) != NULL) {
		TAILQ_REMOVE(messages, msg, me_entry);
		message_free(msg);
	}
	free(messages);
}

static int
message_flags_parse(struct message_flags *flags, const char *path)
{
	const char *p;
	int i;

	p = strrchr(path, ':');
	if (p == NULL)
		return 0;
	if (p[1] != '2' || p[2] != ',') {
		warnx("%s: invalid flags", path);
		return 1;
	}

	for (i = 3; p[i] != '\0'; i++) {
		if (message_flags_set(flags, p[i], 1))
			return 1;
	}

	return 0;
}

static int
message_flags_resolve(unsigned char flag, unsigned int *idx, unsigned int *mask)
{
	if (isupper(flag)) {
		*idx = 0;
		*mask = 1 << (flag - 'A');
		return 0;
	}
	if (islower(flag)) {
		*idx = 1;
		*mask = 1 << (flag - 'a');
		return 0;
	}

	warnx("%c: unknown flag", flag);
	return 1;
}

static void
message_headers_realloc(struct message *msg)
{
	size_t newsize;

	if (msg->me_headers.h_nmemb + 1 < msg->me_headers.h_size)
		return;

	if (msg->me_headers.h_size == 0)
		newsize = 16;
	else
		newsize = msg->me_headers.h_size * 2;

	msg->me_headers.h_v = reallocarray(msg->me_headers.h_v, newsize,
	    sizeof(*msg->me_headers.h_v));
	if (msg->me_headers.h_v == NULL)
		err(1, NULL);
	msg->me_headers.h_size = newsize;
}

static int
message_is_content_type(const struct message *msg, const char *needle)
{
	const char *type;
	size_t len;

	type = message_get_header1(msg, "Content-Type");
	if (type == NULL)
		return 0;

	len = strlen(needle);
	if (strncmp(type, needle, len) ||
	    (type[len] != ';' && type[len] != '\0'))
		return 0;

	return 1;
}

static const char *
message_parse_headers(struct message *msg)
{
	char *buf, *keybeg, *keyend, *valbeg, *valend;

	buf = msg->me_buf;
	while (findheader(buf, &keybeg, &keyend, &valbeg, &valend) == 0) {
		size_t i = msg->me_headers.h_nmemb;

		*keyend = '\0';
		*valend = '\0';

		message_headers_realloc(msg);

		msg->me_headers.h_v[i].id = msg->me_headers.h_nmemb;
		msg->me_headers.h_v[i].flags = 0;
		msg->me_headers.h_v[i].key = keybeg;
		msg->me_headers.h_v[i].val = valbeg;
		msg->me_headers.h_v[i].values = NULL;
		msg->me_headers.h_nmemb++;

		buf = valend + 1;
	}
	if (msg->me_headers.h_nmemb > 0)
		qsort(msg->me_headers.h_v, msg->me_headers.h_nmemb,
		    sizeof(*msg->me_headers.h_v), cmpheaderkey);

	for (; *buf == '\n'; buf++)
		continue;
	return buf;
}

static int
cmpheaderid(const void *p1, const void *p2)
{
	const struct header *h1 = p1;
	const struct header *h2 = p2;

	if (h1->id < h2->id)
		return -1;
	if (h1->id > h2->id)
		return 1;
	return 0;
}

static int
cmpheaderkey(const void *p1, const void *p2)
{
	const struct header *h1 = p1;
	const struct header *h2 = p2;

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
	i++;	/* consume ';' */

	/* Skip leading whitespace in value. */
	i += nspaces(&str[i]);
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
		cmp = cmpheaderkey(&needle, headers + mi);
		if (cmp == 0) {
			/* Move backwards to the first matching element. */
			for (; mi > 0; mi--)
				if (cmpheaderkey(&needle, headers + mi - 1))
					break;
			for (i = mi; i < nmemb; i++)
				if (cmpheaderkey(&needle, headers + i))
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

static int
parseattachments(const struct message *msg, struct message_list *attachments,
    int depth)
{
	struct message *attach;
	const char *b, *beg, *body, *end, *type;
	char *boundary;
	int term;

	if (depth > 4) {
		warnx("%s: message contains too many nested attachments",
		    msg->me_path);
		return 1;
	}

	type = message_get_header1(msg, "Content-Type");
	if (type == NULL)
		return 0;
	switch (parseboundary(type, &boundary)) {
	case 0:
		return 0;
	case -1:
		warnx("%s: invalid boundary", msg->me_path);
		return 1;
	}

	log_debug("%s: boundary=%s, depth=%d\n", __func__, boundary, depth);

	body = msg->me_body;
	beg = end = NULL;
	term = 0;
	while (!term) {
		b = findboundary(boundary, body, &term);
		if (b == NULL)
			break;
		if (beg == NULL)
			beg = b = skipline(b);
		else
			end = b;
		body = b;
		if (beg == NULL || end == NULL)
			continue;

		attach = calloc(1, sizeof(*attach));
		if (attach == NULL)
			err(1, NULL);
		attach->me_buf = strndup(beg, end - beg);
		if (attach->me_buf == NULL)
			err(1, NULL);
		attach->me_path = msg->me_path;
		attach->me_body = message_parse_headers(attach);
		TAILQ_INSERT_TAIL(attachments, attach, me_entry);

		if (parseattachments(attach, attachments, depth + 1)) {
			term = 0;
			break;
		}

		beg = end = NULL;
	}
	free(boundary);

	return term ? 0 : 1;
}

static const char *
findboundary(const char *boundary, const char *s, int *term)
{
	const char *beg;
	size_t len;
	int skip = 0;

	len = strlen(boundary);

	for (;;) {
		*term = 0;

		if (skip)
			s = skipline(s);
		if (*s == '\0')
			break;
		skip = 1;
		beg = s;

		if (strncmp(s, "--", 2))
			continue;
		s += 2;

		if (strncmp(s, boundary, len))
			continue;
		s += len;

		if (strncmp(s, "--", 2) == 0) {
			s += 2;
			*term = 1;
		}
		if (*s == '\n')
			return beg;
	}

	return NULL;
}

static int
parseboundary(const char *str, char **boundary)
{
	const char *needle, *p;
	size_t len;

	needle = "multipart/";
	len = strlen(needle);
	if (strncmp(str, needle, len))
		return 0;
	str += len;
	for (; *str != '\0' && *str != ';'; str++)
		continue;
	if (*str == '\0')
		return 0;
	str++;
	str += nspaces(str);

	needle = "boundary=\"";
	len = strlen(needle);
	if (strncmp(str, needle, len))
		return 0;
	str += len;
	p = str;
	for (; *p != '\0' && *p != '"'; p++)
		continue;
	if (*p != '"')
		return -1;
	len = p - str;
	if (len == 0)
		return -1;
	*boundary = strndup(str, len);
	if (*boundary == NULL)
		err(1, NULL);
	return 1;
}

static char *
b64decode(const char *str)
{
	unsigned char *buf;
	size_t siz;
	int n;

	siz = strlen(str) + 1;
	buf = malloc(siz);
	if (buf == NULL)
		err(1, NULL);
	n = b64_pton(str, buf, siz);
	if (n == -1) {
		free(buf);
		return NULL;
	}
	buf[n] = '\0';

	return (char *)buf;
}

static const char *
skipline(const char *s)
{
	for (; *s != '\0' && *s != '\n'; s++)
		continue;
	if (*s != '\0')
		s++;
	return s;
}

static ssize_t
strflags(unsigned int flags, unsigned char offset, char *buf, size_t bufsiz)
{
	size_t i = 0;
	unsigned int bit = 0;

	for (; flags > 0; flags >>= 1, bit++) {
		if ((flags & 0x1) == 0)
			continue;

		if (i >= bufsiz - 1)
			return -1;
		buf[i++] = offset + bit;
	}

	return i;
}

static int
strword(const char *haystack, const char *needle)
{
	const char *beg, *p;
	size_t len;

	beg = haystack;
	len = strlen(needle);
	for (;;) {
		p = strstr(haystack, needle);
		if (p == NULL)
			return 0;
		if ((p == beg || p[-1] == ' ') &&
		    (p[len] == '\0' || p[len] == ' '))
			return 1;

		haystack = p + len;
	}

	return 0;
}
