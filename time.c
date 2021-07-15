#include "config.h"

#include <err.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

static const char	*timeparse(const char *, struct tm *);
static int		 tzparse(const char *, time_t *,
    const struct environment *);
static int		 tzabbr(const char *, time_t *,
    const struct environment *);
static int		 tzoff(const char *, time_t *);

static const char	*formats[] = {
	"%a, %d %b %Y %H:%M:%S",
	"%a, %d %b %Y %H:%M",
	"%d %b %Y %H:%M:%S",
	NULL,
};

/*
 * Format the given timestamp into a human readable representation.
 */
char *
time_format(time_t tim, char *buf, size_t bufsiz)
{
	struct tm *tm;

	tm = localtime(&tim);
	if (strftime(buf, bufsiz, formats[0], tm) == 0) {
		warnc(ENAMETOOLONG, "%s", __func__);
		return NULL;
	}

	return buf;
}

/*
 * Parse the given formatted timestamp.
 * Returns zero on success, non-zero otherwise.
 */
int
time_parse(const char *str, time_t *res, const struct environment *env)
{
	struct tm tm;
	const char *end;
	time_t tim, tz;

	memset(&tm, 0, sizeof(tm));
	end = timeparse(str, &tm);
	if (end == NULL)
		return 1;
	/* Let mktime(3) figure out if DST is in effect. */
	tm.tm_isdst = -1;
	tim = mktime(&tm);
	if (tim == -1) {
		warnc(EINVAL, "mktime: %s", str);
		return 1;
	}

	end += nspaces(end);
	if (tzparse(end, &tz, env))
		return 1;

	*res = tim - tz + env->ev_tz.t_offset;
	return 0;
}

static const char *
timeparse(const char *str, struct tm *tm)
{
	const char *end;
	int i;

	for (i = 0; formats[i] != NULL; i++) {
		end = strptime(str, formats[i], tm);
		if (end != NULL)
			return end;
	}

	warnc(EINVAL, "strptime: %s", str);
	return NULL;
}

static int
tzparse(const char *str, time_t *tz, const struct environment *env)
{
	if (tzoff(str, tz) && tzabbr(str, tz, env)) {
		warnc(EINVAL, "%s: %s", __func__, str);
		return 1;
	}
	return 0;
}

static int
tzabbr(const char *str, time_t *tz, const struct environment *env)
{
	struct tm *tm;
	int error = 0;

	if (strlen(str) == 0)
		return 1;

	if (setenv("TZ", str, 1) == -1) {
		warn("setenv: TZ");
		return 1;
	}
	tzset();
	tm = localtime(&env->ev_now);
	if (tm == NULL) {
		warn("localtime");
		error = 1;
	} else {
		*tz = tm->tm_gmtoff;
	}

	/* Reset timezone. */
	switch (env->ev_tz.t_state) {
	case TZ_STATE_LOCAL:
		if (unsetenv("TZ") == -1) {
			warn("unsetenv: TZ");
			return 1;
		}
		break;
	case TZ_STATE_UTC:
	case TZ_STATE_SET:
		if (setenv("TZ", env->ev_tz.t_buf, 1) == -1) {
			warn("setenv: TZ");
			return 1;
		}
		break;
	}
	tzset();

	return error;
}

static int
tzoff(const char *str, time_t *tz)
{
	int i, sign;
	int hours = 0;
	int minutes = 0;

	if (str[0] == '+')
		sign = 1;
	else if (str[0] == '-')
		sign = -1;
	else
		return 1;

	for (i = 1; i >= 0; i--) {
		str++;
		if (str[0] < '0' || str[0] > '9')
			return 1;
		hours *= 10;
		hours += str[0] - '0';
	}
	if (hours > 23)
		return 1;

	for (i = 1; i >= 0; i--) {
		str++;
		if (str[0] < '0' || str[0] > '9')
			return 1;
		minutes *= 10;
		minutes += str[0] - '0';
	}
	if (minutes > 59)
		return 1;

	*tz = sign * (hours * 60 * 60 + minutes * 60);
	return 0;
}
