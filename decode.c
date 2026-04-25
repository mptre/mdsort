#include "decode.h"
#include "config.h"
#include <sys/types.h> /* ssize_t */
#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include "libks/arena-buffer.h"
#include "libks/arena.h"
#include "libks/buffer.h"
#include "libks/compiler.h"

static ssize_t	b64_pton(const char *, unsigned char *, size_t);
static void	quoted_printable_decode_buffer(struct buffer *, const char *,
    size_t, int);

static int	htoa(char, char *);

const char *
base64_decode(const char *str, struct arena_scope *s)
{
	size_t len = strlen(str);
	uint8_t *dec = arena_malloc(s, len + 1);
	ssize_t n = b64_pton(str, dec, len + 1);
	if (n == -1)
		return NULL;
	dec[n] = '\0';
	return (char *)dec;
}

const char *
quoted_printable_decode(const char *str, struct arena_scope *s)
{
	struct buffer *bf;
	size_t len;

	len = strlen(str);
	bf = arena_buffer_alloc(s, len);
	quoted_printable_decode_buffer(bf, str, len, 0);
	return buffer_str(bf);
}

/*
 * Decode header which can either be encoded using quoted printable or base64,
 * see RFC 2047.
 */
const char *
rfc2047_decode(const char *str, struct arena_scope *s)
{
	struct buffer *bf;
	const char *es = str;

	bf = arena_buffer_alloc(s, strlen(str));

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
			if (*es == '\0')
				goto err;
			enc = *es;
			es += 1;
			if (*es != '?')
				goto err;
			es += 1;

			ee = strstr(es, "?=");
			if (ee == NULL)
				goto err;

			len = (size_t)(ee - es);
			switch (toupper((unsigned char)enc)) {
			case 'B': {
				const char *dst, *src;

				/* b64decode() requires a NUL-terminator. */
				src = arena_strndup(s, es, len);
				dst = base64_decode(src, s);
				if (dst == NULL)
					goto err;
				buffer_printf(bf, "%s", dst);
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

	return buffer_str(bf);

err:
	return arena_strdup(s, str);
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

		buffer_putc(bf, (char)((hi << 4) | lo));
		i += 2;
	}
}

static ssize_t
b64_pton(const char *src, unsigned char *target, size_t targsize)
{
	static const char Base64[] =
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	static const char Pad64 = '=';
	size_t tarindex;
	int ch, state;
	unsigned char nextbyte;
	const char *pos;

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
				target[tarindex] =
				    (uint8_t)((pos - Base64) << 2);
			}
			state = 1;
			break;
		case 1:
			if (target) {
				if (tarindex >= targsize)
					return -1;
				target[tarindex] |= (pos - Base64) >> 4;
				nextbyte =
				    (uint8_t)(((pos - Base64) & 0x0f) << 4);
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
				nextbyte =
				    (uint8_t)(((pos - Base64) & 0x03) << 6);
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
			for (; ch != '\0'; ch = (unsigned char)*src++) {
				if (!isspace(ch))
					break;
			}
			/* Make sure there is another trailing = sign. */
			if (ch != Pad64)
				return -1;
			ch = (unsigned char)*src++;		/* Skip the = */
			/* Fall through to "single trailing =" case. */
			FALLTHROUGH;

		case 3:		/* Valid, means two bytes of info */
			/*
			 * We know this char is an =.  Is there anything but
			 * whitespace after it?
			 */
			for (; ch != '\0'; ch = (unsigned char)*src++) {
				if (!isspace(ch))
					return -1;
			}

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

	return (ssize_t)tarindex;
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
