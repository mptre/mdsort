#include "decode.h"

#include "config.h"

/* b64_pton() */
#include <netinet/in.h>
#include <resolv.h>

#include <ctype.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

#include "buffer.h"

static void	quoted_printable_decode_buffer(struct buffer *, const char *,
    size_t, int);

static int	htoa(char, char *);

char *
base64_decode(const char *str)
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

char *
quoted_printable_decode(const char *str)
{
	struct buffer *bf;
	char *dec;
	size_t len;

	len = strlen(str);
	bf = buffer_alloc(len);
	if (bf == NULL)
		err(1, NULL);
	quoted_printable_decode_buffer(bf, str, len, 0);
	dec = buffer_release(bf);
	buffer_free(bf);
	return dec;
}

/*
 * Decode header which can either be encoded using quoted printable or base64,
 * see RFC 2047.
 */
char *
rfc2047_decode(const char *str)
{
	struct buffer *bf = NULL;
	const char *qs;
	char *dec;

	qs = str;
	for (;;) {
		const char *p, *qe;
		size_t len;
		char enc;

		p = qs;
		while (isspace((unsigned char)p[0]))
			p++;
		if (strncmp(p, "=?", 2) != 0)
			break;
		qs = &p[2];	/* consume "=?" */
		qs = strchr(qs, '?');
		if (qs == NULL)
			goto out;
		qs += 1;
		enc = *qs;
		qs += 1;
		if (*qs != '?')
			goto out;
		qs += 1;

		qe = strstr(qs, "?=");
		if (qe == NULL)
			goto out;

		if (bf == NULL) {
			bf = buffer_alloc(128);
			if (bf == NULL)
				err(1, NULL);
		}
		len = (size_t)(qe - qs);
		switch (toupper((unsigned char)enc)) {
		case 'B': {
			char *dst, *src;

			/* b64decode() requires a NUL-terminator. */
			src = strndup(qs, len);
			if (src == NULL)
				goto out;
			dst = base64_decode(src);
			free(src);
			if (dst == NULL)
				goto out;
			buffer_printf(bf, "%s", dst);
			free(dst);
			break;
		}

		case 'Q':
			quoted_printable_decode_buffer(bf, qs, len, 1);
			break;

		default:
			goto out;
		}

		qs = &qe[2];	/* consume "?=" */
	}

out:
	if (bf == NULL) {
		dec = strdup(str);
		if (dec == NULL)
			err(1, NULL);
		return dec;
	}

	buffer_printf(bf, "%s", qs);
	buffer_putc(bf, '\0');
	dec = buffer_release(bf);
	buffer_free(bf);
	return dec;
}

static void
quoted_printable_decode_buffer(struct buffer *bf, const char *str, size_t len,
    int dospace)
{
	size_t i = 0;

	while (i < len) {
		char hi, lo;

		if (str[i] == '_' && dospace) {
			buffer_putc(bf, ' ');
			i++;
			continue;
		}
		if (str[i] != '=') {
			buffer_putc(bf, str[i++]);
			continue;
		}

		if (i + 1 == len) {
			/* '=' followed by nothing, copy as is. */
			buffer_putc(bf, str[i++]);
			break;
		}

		i++; /* consume '=' */
		if (str[i] == '\n') {
			/* Soft line break. */
			i++;
			continue;
		}

		if (i + 1 == len ||
		    htoa(str[i], &hi) || htoa(str[i + 1], &lo)) {
			/*
			 * Either too few characters to make up a hexadecimal
			 * representation or decoding failed. Copy the '=' and
			 * let subsequent iterations copy the following
			 * characters as is.
			 */
			buffer_putc(bf, '=');
			continue;
		}

		buffer_putc(bf, (hi << 4) | lo);
		i += 2;
	}
	buffer_putc(bf, '\0');
}

/*
 * Hexadecimal to ASCII.
 */
static int
htoa(char c, char *res)
{
	if (c >= 'A' && c <= 'F') {
		*res = 10 + (c - 'A');
		return 0;
	}
	if (c >= '0' && c <= '9') {
		*res = c - '0';
		return 0;
	}
	return 1;
}
