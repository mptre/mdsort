#include "message.h"

#include "config.h"

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>	/* SIZE_MAX */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "buffer.h"
#include "decode.h"
#include "extern.h"
#include "vector.h"

struct message {
	char			 me_path[PATH_MAX];	/* full path */
	char			 me_name[NAME_MAX + 1];	/* file name */
	const char		*me_body;
	char			*me_buf;
	char			*me_buf_dec;		/* decoded body */
	int			 me_fd;
	unsigned int		 me_flags;
#define MESSAGE_FLAG_ATTACHMENT	0x00000001u

	struct message_flags	 me_mflags;		/* maildir flags */

	VECTOR(struct header)	 me_headers;
	VECTOR(struct message)	 me_attachments;
};

struct header {
	unsigned int	 id;

	unsigned int	 flags;
#define HEADER_FLAG_DIRTY	0x00000001u	/* val must be freed */

	const char	*key;
	char		*val;
	VECTOR(char *)	 values;	/* all values for key */
};

struct slice {
	char	*s_beg;
	char	*s_end;
};

static int		 message_flags_parse(struct message_flags *,
    const char *);
static int		 message_flags_resolve(struct message_flags *, char,
    unsigned int **, unsigned int *);
static struct header	*message_headers_alloc(struct message *);
static int		 message_is_content_type(const struct message *,
    const char *);
static const char	*message_parse_headers(struct message *);
static const char	*message_decode_body(struct message *,
    const struct message *);

static int	 cmpheaderid(const struct header *, const struct header *);
static int	 cmpheaderkey(const struct header *, const struct header *);
static int	 findheader(char *, struct slice *, struct slice *);
static ssize_t	 searchheader(const struct header *, size_t, const char *,
    size_t *);
static char	*decodeheader(const char *);
static char	*unfoldheader(const char *);

static int		 parseattachments(struct message *, struct message *,
    int);
static const char	*findboundary(const char *, const char *, int *);
static int		 parseboundary(const char *, char **);

static const char	*skipline(const char *);
static char		*skipseparator(char *);
static ssize_t		 strflags(unsigned int, unsigned char, char *, size_t);
static int		 writefd(const char *);

char *
message_flags_str(const struct message_flags *mf, char *buf, size_t bufsiz)
{
	ssize_t n;
	size_t i = 0;

	/* Ensure room for at least the empty set of flags ":2,\0". */
	if (bufsiz < 4)
		goto err;

	buf[i++] = ':';
	buf[i++] = '2';
	buf[i++] = ',';
	n = strflags(mf->mf_upper, 'A', &buf[i], bufsiz - i);
	if (n == -1)
		goto err;
	i += (size_t)n;
	n = strflags(mf->mf_lower, 'a', &buf[i], bufsiz - i);
	if (n == -1)
		goto err;
	i += (size_t)n;
	buf[i] = '\0';
	return buf;

err:
	warnc(ENAMETOOLONG, "%s", __func__);
	return NULL;
}

int
message_flags_isset(const struct message_flags *mf, char flag)
{
	unsigned int *flags;
	unsigned int mask;

	if (message_flags_resolve((struct message_flags *)mf, flag, &flags,
	    &mask))
		return 0;
	if (*flags & mask)
		return 1;
	return 0;
}

int
message_flags_clr(struct message_flags *mf, char flag)
{
	unsigned int *flags;
	unsigned int mask;

	if (message_flags_resolve(mf, flag, &flags, &mask))
		return 1;
	*flags &= ~mask;
	return 0;
}

int
message_flags_set(struct message_flags *mf, char flag)
{
	unsigned int *flags;
	unsigned int mask;

	if (message_flags_resolve(mf, flag, &flags, &mask))
		return 1;
	*flags |= mask;
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
	struct buffer *bf;
	struct message *msg;
	char *buf;
	size_t siz;
	int fd;

	fd = openat(dirfd, path, O_RDONLY | O_CLOEXEC);
	if (fd == -1) {
		warn("open: %s/%s", dir, path);
		return NULL;
	}
	bf = buffer_read_fd(fd);
	if (bf == NULL) {
		warn("%s", path);
		close(fd);
		return NULL;
	}
	buf = buffer_str(bf);
	buffer_free(bf);

	msg = calloc(1, sizeof(*msg));
	if (msg == NULL)
		err(1, NULL);
	msg->me_fd = fd;
	msg->me_buf = buf;
	if (VECTOR_INIT(msg->me_headers) == NULL)
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
	if (msg == NULL)
		return;

	if (msg->me_attachments != NULL) {
		while (!VECTOR_EMPTY(msg->me_attachments)) {
			struct message *attach;

			attach = VECTOR_POP(msg->me_attachments);
			message_free(attach);
		}
		VECTOR_FREE(msg->me_attachments);
	}

	while (!VECTOR_EMPTY(msg->me_headers)) {
		struct header *hdr;

		hdr = VECTOR_POP(msg->me_headers);
		if (hdr->values != NULL) {
			while (!VECTOR_EMPTY(hdr->values))
				free(*VECTOR_POP(hdr->values));
			VECTOR_FREE(hdr->values);
		}
		if (hdr->flags & HEADER_FLAG_DIRTY)
			free(hdr->val);
	}
	VECTOR_FREE(msg->me_headers);

	if (msg->me_fd != -1)
		close(msg->me_fd);
	free(msg->me_buf);
	free(msg->me_buf_dec);
	if ((msg->me_flags & MESSAGE_FLAG_ATTACHMENT) == 0)
		free(msg);
}

int
message_write(struct message *msg, int fd)
{
	FILE *fh;
	unsigned int i;
	int error = 0;
	int newfd;

	/*
	 * Since fclose(3) uncondtionally closes the file descriptor, operate on
	 * a duplicate in order to prevent side effects.
	 */
	newfd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
	if (newfd == -1) {
		warn("dup");
		return 1;
	}

	fh = fdopen(newfd, "we");
	if (fh == NULL) {
		warn("fdopen");
		close(newfd);
		return 1;
	}

	/* Preserve ordering of headers. */
	VECTOR_SORT(msg->me_headers, cmpheaderid);

	for (i = 0; i < VECTOR_LENGTH(msg->me_headers); i++) {
		const struct header *hdr = &msg->me_headers[i];

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
	} else if (fsync(newfd) == -1) {
		warn("fsync");
		error = 1;
	}

out:
	if (fclose(fh) == EOF) {
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
message_get_fd(struct message *msg, const struct environment *env, int dobody)
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
			ssize_t nw;

			nw = write(fd, body, len);
			if (nw == -1) {
				warn("write");
				close(fd);
				return -1;
			}
			len -= (size_t)nw;
			body += nw;
		}
	} else if (msg->me_flags & MESSAGE_FLAG_ATTACHMENT) {
		fd = writefd(env->ev_tmpdir);
		if (fd == -1)
			return -1;
		if (message_write(msg, fd)) {
			close(fd);
			return -1;
		}
	} else {
		fd = fcntl(msg->me_fd, F_DUPFD_CLOEXEC, 0);
		if (fd == -1) {
			warn("dup");
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
	VECTOR(struct message *) attachments;
	const struct message *found = NULL;
	size_t i;

	if (msg->me_buf_dec != NULL)
		return msg->me_buf_dec;
	if (!message_is_content_type(msg, "multipart/alternative"))
		return message_decode_body(msg, msg);

	/* Attachment parsing errors are considered fatal. */
	attachments = message_get_attachments(msg);
	if (attachments == NULL)
		return NULL;

	/* Scan attachments, favor plain text over HTML. */
	for (i = 0; i < VECTOR_LENGTH(attachments); i++) {
		const struct message *attach = attachments[i];

		if (message_is_content_type(attach, "text/plain")) {
			found = attach;
			break;
		}

		if (message_is_content_type(attach, "text/html")) {
			if (found == NULL)
				found = attach;
		}
	}
	message_free_attachments(attachments);
	if (found == NULL)
		return msg->me_body;

	(void)message_decode_body(msg, found);

	return msg->me_buf_dec;
}

char *const *
message_get_header(const struct message *msg, const char *header)
{
	struct header *hdr;
	ssize_t idx;
	size_t nfound;

	idx = searchheader(msg->me_headers, VECTOR_LENGTH(msg->me_headers),
	    header, &nfound);
	if (idx == -1)
		return NULL;

	hdr = &msg->me_headers[idx];
	if (hdr->values == NULL) {
		struct header *tmp;
		size_t i;

		if (VECTOR_INIT(hdr->values) == NULL)
			err(1, NULL);
		if (VECTOR_RESERVE(hdr->values, nfound) == NULL)
			err(1, NULL);
		for (i = 0, tmp = hdr; i < nfound; i++, tmp++)
			*VECTOR_ALLOC(hdr->values) = decodeheader(tmp->val);
	}
	return hdr->values;
}

const char *
message_get_header1(const struct message *msg, const char *header)
{
	VECTOR(char *const) values;

	values = message_get_header(msg, header);
	if (values == NULL || VECTOR_EMPTY(values))
		return NULL;
	return values[0];
}

void
message_set_header(struct message *msg, const char *header, char *val)
{
	struct header *hdr;
	ssize_t idx;
	size_t nfound;

	idx = searchheader(msg->me_headers, VECTOR_LENGTH(msg->me_headers),
	    header, &nfound);
	if (idx == -1) {
		hdr = message_headers_alloc(msg);
		hdr->flags = HEADER_FLAG_DIRTY;
		hdr->key = header;
		hdr->val = val;
		VECTOR_SORT(msg->me_headers, cmpheaderkey);
	} else {
		if (nfound > 1) {
			size_t i = (size_t)idx;
			size_t tail;

			/*
			 * Multiple occurrences of the given header.
			 * Remove all occurrences except the first one.
			 */
			tail = VECTOR_LENGTH(msg->me_headers) - (i + nfound);
			memmove(&msg->me_headers[i + 1],
			    &msg->me_headers[i + nfound],
			    tail * sizeof(*msg->me_headers));
			for (; nfound > 1; nfound--)
				VECTOR_POP(msg->me_headers);
		}

		hdr = &msg->me_headers[idx];
		if (hdr->flags & HEADER_FLAG_DIRTY)
			free(hdr->val);
		else
			hdr->flags |= HEADER_FLAG_DIRTY;
		hdr->val = val;
		if (hdr->values != NULL) {
			while (!VECTOR_EMPTY(hdr->values))
				free(*VECTOR_POP(hdr->values));
		}
		VECTOR_FREE(hdr->values);
	}
}

/*
 * Associate the given message with a new file. If fd is not equal to -1, it is
 * expected to reference the new file.
 */
int
message_set_file(struct message *msg, const char *path, const char *name,
    int fd)
{
	size_t siz;

	if (FAULT("message_set_file"))
		return 1;

	if (pathjoin(msg->me_path, sizeof(msg->me_path), path, name) == NULL) {
		warnc(ENAMETOOLONG, "%s", __func__);
		return 1;
	}

	siz = sizeof(msg->me_name);
	if (strlcpy(msg->me_name, name, siz) >= siz) {
		warnc(ENAMETOOLONG, "%s", __func__);
		return 1;
	}

	if (fd != -1) {
		if (msg->me_fd != -1)
			close(msg->me_fd);
		msg->me_fd = fd;
	}

	return 0;
}

const char *
message_get_path(const struct message *msg)
{
	return msg->me_path;
}

struct message_flags *
message_get_flags(const struct message *msg)
{
	return (struct message_flags *)&msg->me_mflags;
}

const char *
message_get_name(const struct message *msg)
{
	return msg->me_name;
}

/*
 * Parse all attachments in message into the given message list.
 * Returns non NULL-on success, even if no attachments where found.
 */
struct message **
message_get_attachments(struct message *msg)
{
	VECTOR(struct message *) attachments;
	size_t i, n;

	if (msg->me_attachments == NULL) {
		if (VECTOR_INIT(msg->me_attachments) == NULL)
			err(1, NULL);
		if (parseattachments(msg, msg, 0))
			return NULL;
	}

	n = VECTOR_LENGTH(msg->me_attachments);
	if (VECTOR_INIT(attachments) == NULL)
		err(1, NULL);
	if (VECTOR_RESERVE(attachments, n) == NULL)
		err(1, NULL);
	for (i = 0; i < n; i++)
		*VECTOR_ALLOC(attachments) = &msg->me_attachments[i];
	return attachments;
}

void
message_free_attachments(struct message **attachments)
{
	VECTOR_FREE(attachments);
}

static int
message_flags_parse(struct message_flags *mf, const char *path)
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
		if (message_flags_set(mf, p[i]))
			return 1;
	}

	return 0;
}

static int
message_flags_resolve(struct message_flags *mf, char flag,
    unsigned int **flags, unsigned int *mask)
{
	if (isupper((unsigned char)flag)) {
		*flags = &mf->mf_upper;
		*mask = 1 << (flag - 'A');
		return 0;
	}
	if (islower((unsigned char)flag)) {
		*flags = &mf->mf_lower;
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

	hdr = VECTOR_CALLOC(msg->me_headers);
	if (hdr == NULL)
		err(1, NULL);
	hdr->id = VECTOR_LENGTH(msg->me_headers);
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
	if (strncmp(type, needle, len) != 0 ||
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

	while (findheader(buf, &ks, &vs)) {
		struct header *hdr = message_headers_alloc(msg);

		hdr->key = ks.s_beg;
		hdr->val = vs.s_beg;

		buf = vs.s_end + 1;
	}
	VECTOR_SORT(msg->me_headers, cmpheaderkey);

	for (; *buf == '\n'; buf++)
		continue;
	return buf;
}

static const char *
message_decode_body(struct message *msg, const struct message *attachment)
{
	const char *enc;

	enc = message_get_header1(attachment, "Content-Transfer-Encoding");
	if (enc != NULL && strcmp(enc, "base64") == 0) {
		msg->me_buf_dec = base64_decode(attachment->me_body);
		if (msg->me_buf_dec == NULL)
			warnx("%s: failed to decode body", msg->me_path);
	} else if (enc != NULL && strcmp(enc, "quoted-printable") == 0) {
		msg->me_buf_dec = quoted_printable_decode(attachment->me_body);
	} else {
		msg->me_buf_dec = strdup(attachment->me_body);
		if (msg->me_buf_dec == NULL)
			err(1, NULL);
	}

	return msg->me_buf_dec;
}

static int
cmpheaderid(const struct header *a, const struct header *b)
{
	if (a->id < b->id)
		return -1;
	if (a->id > b->id)
		return 1;
	return 0;
}

static int
cmpheaderkey(const struct header *a, const struct header *b)
{
	return strcasecmp(a->key, b->key);
}

static char *
decodeheader(const char *str)
{
	char *d, *u;

	u = unfoldheader(str);
	d = rfc2047_decode(u);
	free(u);
	return d;
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

		for (; *str == '\t'; str++)
			continue;
		end = strchr(str, '\n');
		if (end == NULL)
			end = str + strlen(str);
		while (str != end)
			dec[i++] = *str++;
		if (*str == '\n')
			str++;
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
			return 0;
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
			return 0;
		i += (size_t)(p - &str[i]);

		/* If '\n' is followed by spaces, assume line continuation. */
		n = nspaces(&str[i + 1]);
		if (n == 0)
			break;
		i += (size_t)(n + 1);
	}
	vs->s_end = str + i;
	*vs->s_end = '\0';

	return 1;
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
	size_t hi, lo;

	*nfound = 0;

	if (nmemb == 0)
		return -1;

	memset(&needle, 0, sizeof(needle));
	needle.key = key;
	lo = 0;
	hi = nmemb - 1;
	while (lo <= hi) {
		size_t mi;
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
			return (ssize_t)beg;
		}
		if (cmp > 0)
			lo = mi + 1;
		else if (mi > 0)
			hi = mi - 1;
		else
			break;
	}
	return -1;
}

static int
parseattachments(struct message *msg, struct message *parent, int depth)
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
		size_t len;

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

		attach = VECTOR_CALLOC(parent->me_attachments);
		if (attach == NULL)
			err(1, NULL);
		attach->me_fd = -1;
		attach->me_flags = MESSAGE_FLAG_ATTACHMENT;
		len = (size_t)(end - beg);
		attach->me_buf = strndup(beg, len);
		if (attach->me_buf == NULL)
			err(1, NULL);
		if (VECTOR_INIT(attach->me_headers) == NULL)
			err(1, NULL);
		(void)strlcpy(attach->me_path, msg->me_path,
		    sizeof(attach->me_path));
		(void)strlcpy(attach->me_name, msg->me_name,
		    sizeof(attach->me_name));
		attach->me_body = message_parse_headers(attach);

		if (parseattachments(attach, parent, depth + 1)) {
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
	size_t len;
	int skip = 0;

	len = strlen(boundary);

	for (;;) {
		const char *beg;

		*term = 0;

		if (skip)
			s = skipline(s);
		if (*s == '\0')
			break;
		skip = 1;
		beg = s;

		if (strncmp(s, "--", 2) != 0)
			continue;
		s += 2;

		if (strncmp(s, boundary, len) != 0)
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
	if (strncmp(str, needle, len) != 0)
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
	if (strncmp(str, needle, len) != 0)
		return 0;
	str += len;
	p = str;
	for (; *p != '\0' && *p != '"'; p++)
		continue;
	if (*p != '"')
		return -1;
	len = (size_t)(p - str);
	if (len == 0)
		return -1;
	*boundary = strndup(str, len);
	if (*boundary == NULL)
		err(1, NULL);
	return 1;
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

	if (strncmp(str, separator, sizeof(separator) - 1) != 0)
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

	return (ssize_t)i;
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
