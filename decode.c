#include "decode.h"

#include "config.h"

#include <ctype.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

#include "buffer.h"

static int	b64_pton(const char *, unsigned char *, size_t);
static void	quoted_printable_decode_buffer(struct buffer *, const char *,
    size_t, int);

static int	htoa(char, char *);

char *
base64_decode(const char *str)
{
	unsigned char *dec = NULL;
	size_t len;
	int n;

	len = strlen(str);
	dec = malloc(len + 1);
	if (dec == NULL)
		err(1, NULL);
	n = b64_pton(str, dec, len + 1);
	if (n == -1) {
		free(dec);
		return NULL;
	}
	dec[n] = '\0';
	return (char *)dec;
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
	buffer_putc(bf, '\0');
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
	struct buffer *bf;
	const char *es = str;
	char *dec;

	bf = buffer_alloc(strlen(str));
	if (bf == NULL)
		err(1, NULL);

	while (*es != '\0') {
		if (strncmp(es, "=?", 2) == 0) {
			const char *ee;
			size_t len;
			char enc;

			es = &es[2];	/* consume "=?" */
			es = strchr(es, '?');
			if (es == NULL)
				goto err;
			es += 1;
			enc = *es;
			es += 1;
			if (*es != '?')
				goto err;
			es += 1;

			ee = strstr(es, "?=");
			if (ee == NULL)
				goto err;

			if (bf == NULL) {
				bf = buffer_alloc(128);
				if (bf == NULL)
					err(1, NULL);
			}
			len = (size_t)(ee - es);
			switch (toupper((unsigned char)enc)) {
			case 'B': {
				char *dst, *src;

				/* b64decode() requires a NUL-terminator. */
				src = strndup(es, len);
				if (src == NULL)
					err(1, NULL);
				dst = base64_decode(src);
				free(src);
				if (dst == NULL)
					goto err;
				buffer_printf(bf, "%s", dst);
				free(dst);
				break;
			}

			case 'Q':
				quoted_printable_decode_buffer(bf, es, len, 1);
				break;

			default:
				goto err;
			}

			es = &ee[2];	/* consume "?=" */

			/* Spaces between encoded words must be ignored. */
			ee = strstr(es, "=?");
			if (ee != NULL) {
				const char *p = es;

				while (isspace((unsigned char)p[0]))
					p++;
				if (p == ee)
					es = p;
			}
		} else {
			buffer_putc(bf, *es++);
		}
	}

	buffer_putc(bf, '\0');
	dec = buffer_release(bf);
	buffer_free(bf);
	return dec;

err:
	buffer_free(bf);
	dec = strdup(str);
	if (dec == NULL)
		err(1, NULL);
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
}

int
b64_pton(const char *src, unsigned char *target, size_t targsize)
{
	static const char Base64[] =
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	static const char Pad64 = '=';
	size_t tarindex;
	int ch, state;
	u_char nextbyte;
	char *pos;

	state = 0;
	tarindex = 0;

	while ((ch = (unsigned char)*src++) != '\0') {
		if (isspace(ch))	/* Skip whitespace anywhere. */
			continue;

		if (ch == Pad64)
			break;

		pos = strchr(Base64, ch);
		if (pos == 0) 		/* A non-base64 character. */
			return -1;

		switch (state) {
		case 0:
			if (target) {
				if (tarindex >= targsize)
					return -1;
				target[tarindex] = (pos - Base64) << 2;
			}
			state = 1;
			break;
		case 1:
			if (target) {
				if (tarindex >= targsize)
					return -1;
				target[tarindex] |= (pos - Base64) >> 4;
				nextbyte = ((pos - Base64) & 0x0f) << 4;
				if (tarindex + 1 < targsize)
					target[tarindex + 1] = nextbyte;
				else if (nextbyte)
					return -1;
			}
			tarindex++;
			state = 2;
			break;
		case 2:
			if (target) {
				if (tarindex >= targsize)
					return -1;
				target[tarindex] |= (pos - Base64) >> 2;
				nextbyte = ((pos - Base64) & 0x03) << 6;
				if (tarindex + 1 < targsize)
					target[tarindex + 1] = nextbyte;
				else if (nextbyte)
					return -1;
			}
			tarindex++;
			state = 3;
			break;
		case 3:
			if (target) {
				if (tarindex >= targsize)
					return -1;
				target[tarindex] |= (pos - Base64);
			}
			tarindex++;
			state = 0;
			break;
		}
	}

	/*
	 * We are done decoding Base-64 chars.  Let's see if we ended
	 * on a byte boundary, and/or with erroneous trailing characters.
	 */

	if (ch == Pad64) {			/* We got a pad char. */
		ch = (unsigned char)*src++;	/* Skip it, get next. */
		switch (state) {
		case 0:		/* Invalid = in first position */
		case 1:		/* Invalid = in second position */
			return -1;

		case 2:		/* Valid, means one byte of info */
			/* Skip any number of spaces. */
			for (; ch != '\0'; ch = (unsigned char)*src++)
				if (!isspace(ch))
					break;
			/* Make sure there is another trailing = sign. */
			if (ch != Pad64)
				return -1;
			ch = (unsigned char)*src++;		/* Skip the = */
			/* Fall through to "single trailing =" case. */
			/* FALLTHROUGH */

		case 3:		/* Valid, means two bytes of info */
			/*
			 * We know this char is an =.  Is there anything but
			 * whitespace after it?
			 */
			for (; ch != '\0'; ch = (unsigned char)*src++)
				if (!isspace(ch))
					return -1;

			/*
			 * Now make sure for cases 2 and 3 that the "extra"
			 * bits that slopped past the last full byte were
			 * zeros.  If we don't check them, they become a
			 * subliminal channel.
			 */
			if (target && tarindex < targsize &&
			    target[tarindex] != 0)
				return -1;
		}
	} else {
		/*
		 * We ended by seeing the end of the string.  Make sure we
		 * have no partial bytes lying around.
		 */
		if (state != 0)
			return -1;
	}

	return tarindex;
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
