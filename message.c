#include "config.h"

/* b64_pton() */
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
#define HEADER_FLAG_DIRTY	0x00000001u	/* val must be freed */

	const char *key;
	char *val;
	struct string_list *values;	/* list of all values for key */
};

struct slice {
	char *s_beg;
	char *s_end;
};

static int message_flags_parse(struct message_flags *, const char *);
static int message_flags_resolve(unsigned char, unsigned int *, unsigned int *);
static struct header *message_headers_alloc(struct message *);
static int message_is_content_type(const struct message *, const char *);
static const char *message_parse_headers(struct message *);
static const char *message_decode_body(struct message *,
    const struct message *);

static int cmpheaderid(const void *, const void *);
static int cmpheaderkey(const void *, const void *);
static int findheader(char *, struct slice *, struct slice *);
static ssize_t searchheader(const struct header *, size_t, const char *,
    size_t *);
static char *unfoldheader(const char *);

static int parseattachments(struct message *, struct message_list *, int);
static const char *findboundary(const char *, const char *, int *);
static int parseboundary(const char *, char **);

static char *b64decode(const char *);
static const char *skipline(const char *);
static char *skipseparator(char *);
static ssize_t strflags(unsigned int, unsigned char, char *, size_t);
static int writefd(const char *);

char *
message_flags_str(const struct message_flags *flags, char *buf, size_t bufsiz)
{
	ssize_t n;
	size_t i = 0;

	/* Ensure room for at least the empty set of flags ":2,\0". */
	if (bufsiz < 4)
		goto err;

	buf[i++] = ':';
	buf[i++] = '2';
	buf[i++] = ',';
	n = strflags(flags->mf_flags[0], 'A', &buf[i], bufsiz - i);
	if (n == -1)
		goto err;
	i += n;
	n = strflags(flags->mf_flags[1], 'a', &buf[i], bufsiz - i);
	if (n == -1)
		goto err;
	i += n;
	buf[i] = '\0';

	return buf;

err:
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
	size_t msgsize = 8192;
	size_t siz;
	int fd;

	fd = openat(dirfd, path, O_RDONLY | O_CLOEXEC);
	if (fd == -1) {
		warn("open: %s/%s", dir, path);
		return NULL;
	}

	msg = calloc(1, sizeof(*msg));
	if (msg == NULL)
		err(1, NULL);
	msg->me_fd = fd;
	msg->me_buf = malloc(msgsize);
	if (msg->me_buf == NULL)
		err(1, NULL);

	if (pathjoin(msg->me_path, sizeof(msg->me_path), dir, path) == NULL) {
		warnc(ENAMETOOLONG, "%s", __func__);
		goto err;
	}
	siz = sizeof(msg->me_name);
	if (strlcpy(msg->me_name, path, siz) >= siz) {
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

		n = read(msg->me_fd, msg->me_buf + msglen,
		    msgsize - msglen - 1);
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

	msg->me_body = message_parse_headers(msg);

	if (message_flags_parse(&msg->me_mflags, msg->me_path))
		goto err;

	return msg;

err:
	message_free(msg);
	return NULL;
}

void
message_free(struct message *msg)
{
	size_t i;

	if (msg == NULL)
		return;

	message_list_free(msg->me_attachments);

	for (i = 0; i < msg->me_headers.h_nmemb; i++) {
		struct header *hdr = &msg->me_headers.h_v[i];

		strings_free(hdr->values);
		if (hdr->flags & HEADER_FLAG_DIRTY)
			free(hdr->val);
	}

	if (msg->me_fd != -1)
		close(msg->me_fd);
	free(msg->me_buf);
	free(msg->me_buf_dec);
	free(msg->me_headers.h_v);
	free(msg);
}

int
message_write(struct message *msg, int fd, unsigned int dosync)
{
	FILE *fh;
	unsigned int i;
	int newfd;
	int error = 0;

	/*
	 * Since fclose(3) uncondtionally closes the file descriptor, operate on
	 * a duplicate in order to prevent side effects.
	 */
	newfd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
	if (newfd == -1) {
		warn("fcntl");
		return 1;
	}

	fh = fdopen(newfd, "we");
	if (fh == NULL) {
		warn("fdopen");
		close(newfd);
		return 1;
	}

	/* Preserve ordering of headers. */
	if (msg->me_headers.h_nmemb > 0)
		qsort(msg->me_headers.h_v, msg->me_headers.h_nmemb,
		    sizeof(*msg->me_headers.h_v), cmpheaderid);

	for (i = 0; i < msg->me_headers.h_nmemb; i++) {
		const struct header *hdr = &msg->me_headers.h_v[i];

		if (fprintf(fh, "%s: %s\n", hdr->key, hdr->val) < 0) {
			warn("fprintf");
			error = 1;
			goto out;
		}
	}

	if (fprintf(fh, "\n%s", msg->me_body) < 0) {
		warn("fprintf");
		error = 1;
		goto out;
	}

	if (fflush(fh) == EOF) {
		warn("fflush");
		error = 1;
	} else if (dosync && fsync(fd) == -1) {
		warn("fsync");
		error = 1;
	}

out:
	if (fclose(fh) == EOF && dosync) {
		warn("fclose");
		error = 1;
	}

	if (FAULT("message_write"))
		error = 1;

	return error;
}

/*
 * Get the file descriptor for the given message. Optionally seeking past the
 * headers to where the body begins. In this case, the body is decoded as well.
 * The caller is responsible for closing the returned file descriptor.
 */
int
message_get_fd(struct message *msg, const struct environment *env,
    int dobody)
{
	int fd;

	if (dobody) {
		const char *body;
		size_t len;

		body = message_get_body(msg);
		if (body == NULL)
			return -1;

		fd = writefd(env->ev_tmpdir);
		if (fd == -1)
			return -1;

		len = strlen(body);
		while (len > 0) {
			ssize_t n;

			n = write(fd, body, len);
			if (n == -1) {
				warn("write");
				close(fd);
				return -1;
			}
			len -= n;
			body += n;
		}
	} else if (msg->me_flags & MESSAGE_FLAG_ATTACHMENT) {
		fd = writefd(env->ev_tmpdir);
		if (fd == -1)
			return -1;
		if (message_write(msg, fd, 0)) {
			close(fd);
			return -1;
		}
	} else {
		fd = fcntl(msg->me_fd, F_DUPFD_CLOEXEC, 0);
		if (fd == -1) {
			warn("fcntl");
			return -1;
		}
	}

	if (lseek(fd, 0, SEEK_SET) == -1) {
		warn("lseek");
		close(fd);
		return -1;
	}

	return fd;
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

	if (msg->me_buf_dec != NULL)
		return msg->me_buf_dec;
	if (!message_is_content_type(msg, "multipart/alternative"))
		return message_decode_body(msg, msg);

	/* Attachment parsing errors are considered fatal. */
	attachments = message_get_attachments(msg);
	if (attachments == NULL)
		return NULL;

	/* Scan attachments, favor plain text over HTML. */
	TAILQ_FOREACH(attach, attachments, me_entry) {
		if (message_is_content_type(attach, "text/plain")) {
			found = attach;
			break;
		}

		if (message_is_content_type(attach, "text/html")) {
			if (found == NULL)
				found = attach;
		}
	}
	if (found == NULL)
		return msg->me_body;

	(void)message_decode_body(msg, found);

	return msg->me_buf_dec;
}

const struct string_list *
message_get_header(const struct message *msg, const char *header)
{
	struct header *hdr, *tmp;
	ssize_t idx;
	size_t i, nfound;

	idx = searchheader(msg->me_headers.h_v, msg->me_headers.h_nmemb,
	    header, &nfound);
	if (idx == -1)
		return NULL;

	hdr = &msg->me_headers.h_v[idx];
	if (hdr->values == NULL) {
		hdr->values = strings_alloc();
		for (i = 0, tmp = hdr; i < nfound; i++, tmp++) {
			char *val;

			val = unfoldheader(tmp->val);
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
		hdr = message_headers_alloc(msg);
		hdr->flags = HEADER_FLAG_DIRTY;
		hdr->key = header;
		hdr->val = val;

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
 * Parse all attachments in message into the given message list.
 * Returns non NULL-on success, even if no attachments where found.
 */
struct message_list *
message_get_attachments(struct message *msg)
{
	if (msg->me_attachments != NULL)
		return msg->me_attachments;

	msg->me_attachments = malloc(sizeof(*msg->me_attachments));
	if (msg->me_attachments == NULL)
		err(1, NULL);
	TAILQ_INIT(msg->me_attachments);

	if (parseattachments(msg, msg->me_attachments, 0)) {
		message_list_free(msg->me_attachments);
		msg->me_attachments = NULL;
		return NULL;
	}
	return msg->me_attachments;
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

	if (isupper((unsigned char)flag)) {
		*idx = 0;
		*mask = 1 << (flag - 'A');
		return 0;
	}
	if (islower((unsigned char)flag)) {
		*idx = 1;
		*mask = 1 << (flag - 'a');
		return 0;
	}

	warnx("%c: unknown flag", flag);
	return 1;
}

static struct header *
message_headers_alloc(struct message *msg)
{
	struct header *hdr;
	size_t newsize;

	if (msg->me_headers.h_nmemb + 1 < msg->me_headers.h_size)
		goto out;

	if (msg->me_headers.h_size == 0)
		newsize = 16;
	else
		newsize = msg->me_headers.h_size * 2;

	msg->me_headers.h_v = reallocarray(msg->me_headers.h_v, newsize,
	    sizeof(*msg->me_headers.h_v));
	if (msg->me_headers.h_v == NULL)
		err(1, NULL);
	msg->me_headers.h_size = newsize;

out:
	hdr = &msg->me_headers.h_v[msg->me_headers.h_nmemb];
	memset(hdr, 0, sizeof(*hdr));
	hdr->id = msg->me_headers.h_nmemb;
	msg->me_headers.h_nmemb++;
	return hdr;
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
	char *buf;
	struct slice ks, vs;

	buf = skipseparator(msg->me_buf);

	while (findheader(buf, &ks, &vs) == 0) {
		struct header *hdr = message_headers_alloc(msg);

		hdr->key = ks.s_beg;
		hdr->val = vs.s_beg;

		buf = vs.s_end + 1;
	}
	if (msg->me_headers.h_nmemb > 0)
		qsort(msg->me_headers.h_v, msg->me_headers.h_nmemb,
		    sizeof(*msg->me_headers.h_v), cmpheaderkey);

	for (; *buf == '\n'; buf++)
		continue;
	return buf;
}

static const char *
message_decode_body(struct message *msg, const struct message *attachment)
{
	const char *encoding;

	encoding = message_get_header1(attachment, "Content-Transfer-Encoding");
	if (encoding != NULL && strcmp(encoding, "base64") == 0) {
		msg->me_buf_dec = b64decode(attachment->me_body);
		if (msg->me_buf_dec == NULL)
			warnx("%s: failed to decode body", msg->me_path);
	} else {
		msg->me_buf_dec = strdup(attachment->me_body);
		if (msg->me_buf_dec == NULL)
			err(1, NULL);
	}
	return msg->me_buf_dec;
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

/*
 * Unfold the given header value by concatenating multiple lines into a single
 * one.
 */
static char *
unfoldheader(const char *str)
{
	char *dec;
	size_t i = 0;

	dec = strdup(str);
	if (dec == NULL)
		err(1, NULL);
	/*
	 * Optimize for the common case where a header does not span multiple
	 * lines.
	 */
	if (strchr(str, '\n') == NULL)
		return dec;

	for (;;) {
		const char *end;

		if (*str == '\0')
			break;

		end = strchr(str, '\n');
		if (end == NULL)
			end = str + strlen(str);
		if (i > 0)
			dec[i++] = ' ';
		while (str != end)
			dec[i++] = *str++;

		for (; isspace((unsigned char)*str); str++)
			continue;
	}
	dec[i] = '\0';
	return dec;
}

static int
findheader(char *str, struct slice *ks, struct slice *vs)
{
	size_t i;

	for (i = 0; str[i] != ':'; i++) {
		if (str[i] == '\0' || isspace((unsigned char)str[i]))
			return 1;
	}
	ks->s_beg = str;
	ks->s_end = str + i;
	*ks->s_end = '\0';

	/* Consume ':' and skip leading spaces in value. */
	i++;
	i += nspaces(&str[i]);
	vs->s_beg = str + i;

	/* Find the end of the value, with respect to line continuations. */
	for (;;) {
		const char *p;
		int n;

		p = strchr(&str[i], '\n');
		if (p == NULL)
			return 1;
		i += p - &str[i];

		/* If '\n' is followed by spaces, assume line continuation. */
		n = nspaces(&str[i + 1]);
		if (n == 0)
			break;
		i += n + 1;
	}
	vs->s_end = str + i;
	*vs->s_end = '\0';

	return 0;
}

/*
 * Search for the header with the name given by key. Returns the index of the
 * first matching element in headers. Otherwise, -1 is returned. If the given
 * header has more than one occurrence, nfound will reflect that.
 */
static ssize_t
searchheader(const struct header *headers, size_t nmemb, const char *key,
    size_t *nfound)
{
	struct header needle;
	ssize_t hi, mi, lo;

	*nfound = 0;

	if (nmemb == 0)
		return -1;

	memset(&needle, 0, sizeof(needle));
	needle.key = key;
	lo = 0;
	hi = nmemb - 1;
	while (lo <= hi) {
		int cmp;

		mi = lo + (hi - lo)/2;
		cmp = cmpheaderkey(&needle, headers + mi);
		if (cmp == 0) {
			size_t beg, end;

			/* Find the first matching header. */
			for (beg = mi; beg > 0; beg--)
				if (cmpheaderkey(&needle, headers + beg - 1))
					break;
			/* Find the header after the last matching one. */
			for (end = mi + 1; end < nmemb; end++)
				if (cmpheaderkey(&needle, headers + end))
					break;
			*nfound = end - beg;
			return beg;
		}
		if (cmp > 0)
			lo = mi + 1;
		else
			hi = mi - 1;
	}
	return -1;
}

static int
parseattachments(struct message *msg, struct message_list *attachments,
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
		attach->me_fd = -1;
		attach->me_flags = MESSAGE_FLAG_ATTACHMENT;
		attach->me_buf = strndup(beg, end - beg);
		if (attach->me_buf == NULL)
			err(1, NULL);
		(void)strlcpy(attach->me_path, msg->me_path,
		    sizeof(attach->me_path));
		(void)strlcpy(attach->me_name, msg->me_name,
		    sizeof(attach->me_name));
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

	for (;;) {
		if (*s == '\0')
			return s;
		if (*s == '\n')
			return ++s;
		s++;
	}
}

/*
 * Skip past mbox separator line. Older versions of OpenSMTPD are known to emit
 * such separator while performing MDA delivery.
 */
static char *
skipseparator(char *str)
{
	const char separator[] = "From ";
	char *p;

	if (strncmp(str, separator, sizeof(separator) - 1))
		return str;

	p = strchr(str, '\n');
	if (p == NULL)
		return str;
	return p + 1;

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

/*
 * Get a writeable file descriptor by creating a temporary file in the given
 * directory. The file is immediately removed in the hopes of returning the last
 * reference to the file.
 */
static int
writefd(const char *dir)
{
	char path[PATH_MAX];
	int fd;

	if (pathjoin(path, sizeof(path), dir, "mdsort-XXXXXXXX") == NULL) {
		warnc(ENAMETOOLONG, "%s", __func__);
		return -1;
	}

	fd = mkstemp(path);
	if (fd == -1) {
		warn("mkstemp");
		return -1;
	}
	if (unlink(path) == -1) {
		warn("unlink: %s", path);
		close(fd);
		return -1;
	}
	return fd;
}
