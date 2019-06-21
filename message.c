#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "extern.h"

#define FLAG(c)		(isupper((c)) ? 1 << ((c) - 'A') : 0)
#define FLAGS_BAD	((unsigned int)-1)

struct header {
	unsigned int id;

	unsigned int flags;
#define HEADER_FLAG_DIRTY	0x1	/* val must be freed */

	const char *key;
	char *val;
	struct string_list *values;	/* list of all values for key */
};

static void message_headers_realloc(struct message *);
static void message_parse_flags(struct message *);
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
static char *parseboundary(const char *);

static const char *skipline(const char *);
static int strword(const char *, const char *);

/*
 * Parse the message located at path.
 *
 * The caller is responsible for freeing the returned memory using
 * message_free().
 */
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
	msg->me_buf = malloc(msgsize);
	if (msg->me_buf == NULL)
		err(1, NULL);
	msg->me_path = path;

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
			close(fd);
			message_free(msg);
			return NULL;
		} else if (n == 0) {
			break;
		}
		msglen += n;
	}
	assert(msglen < msgsize);
	msg->me_buf[msglen] = '\0';
	close(fd);

	msg->me_body = message_parse_headers(msg);

	message_parse_flags(msg);

	return msg;
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

const char *
message_get_flags(const struct message *msg)
{
	static char buf[32];
	const char *p;
	unsigned int flags;
	int bit = 0;
	int i = 0;

	if (msg->me_flags == FLAGS_BAD) {
		/*
		 * Parsing the flags failed, just give back the flags in its
		 * original form.
		 */
		p = strrchr(msg->me_path, ':');
		if (p == NULL)
			return "";
		return p;
	} else if (msg->me_flags == 0) {
		return "";
	}

	buf[i++] = ':';
	buf[i++] = '2';
	buf[i++] = ',';
	for (flags = msg->me_flags; flags > 0; flags >>= 1) {
		if (flags & 0x1)
			buf[i++] = 'A' + bit;
		bit++;
	}
	buf[i] = '\0';

	return buf;
}

int
message_has_flags(const struct message *msg, unsigned char flag)
{
	if (msg->me_flags == FLAGS_BAD)
		return -1;
	if (msg->me_flags & FLAG(flag))
		return 1;
	return 0;
}

void
message_set_flags(struct message *msg, unsigned char flag, int add)
{
	if (msg->me_flags == FLAGS_BAD)
		return;

	if (add)
		msg->me_flags |= FLAG(flag);
	else
		msg->me_flags &= ~FLAG(flag);
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

struct message_list *
message_get_attachments(const struct message *msg)
{
	struct message_list *attachments;

	attachments = malloc(sizeof(*attachments));
	if (attachments == NULL)
		err(1, NULL);
	TAILQ_INIT(attachments);

	if (parseattachments(msg, attachments, 0) ||
	    TAILQ_EMPTY(attachments)) {
		message_list_free(attachments);
		return NULL;
	}

	return attachments;
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

static void
message_parse_flags(struct message *msg)
{
	const char *p;

	p = strrchr(msg->me_path, ':');
	if (p == NULL) {
		msg->me_flags = 0;
		return;
	} else if (p[1] != '2' || p[2] != ',') {
		msg->me_flags = FLAGS_BAD;
		return;
	}

	for (p += 3; *p != '\0'; p++) {
		if (isupper(*p)) {
			message_set_flags(msg, *p, 1);
		} else {
			msg->me_flags = FLAGS_BAD;
			log_debug("%s: %s: invalid flags",
			    __func__, msg->me_path);
			return;
		}
	}
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

	if (depth > 8) {
		warnx("%s: message contains too many nested attachments",
		    msg->me_path);
		return 1;
	}

	type = message_get_header1(msg, "Content-Type");
	if (type == NULL)
		return 0;
	boundary = parseboundary(type);
	if (boundary == NULL)
		return 0;

	log_debug("%s: boundary=%s, depth=%d\n", __func__, boundary, depth);

	body = msg->me_body;
	beg = end = NULL;
	term = 0;
	for (;;) {
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
		if (term)
			break;
	}
	free(boundary);
	if (!term)
		return 1;
	return 0;

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

static char *
parseboundary(const char *str)
{
	const char *needle, *p;
	char *boundary;
	size_t len;

	needle = "multipart/";
	len = strlen(needle);
	if (strncmp(str, needle, len))
		return NULL;
	str += len;
	for (; *str != '\0' && *str != ';'; str++)
		continue;
	if (*str == '\0')
		return NULL;
	str++;
	str += nspaces(str);

	needle = "boundary=\"";
	len = strlen(needle);
	if (strncmp(str, needle, len))
		return NULL;
	str += len;
	p = str;
	for (; *p != '\0' && *p != '"'; p++)
		continue;
	len = p - str;
	if (len == 0)
		return NULL;
	boundary = strndup(str, len);
	if (boundary == NULL)
		err(1, NULL);
	return boundary;
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
