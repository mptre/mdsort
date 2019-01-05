#include "config.h"

#include <err.h>
#include <errno.h>
#include <string.h>

#include "extern.h"

static const char *timeparse(const char *, struct tm *);
static int tzparse(const char *, time_t *);
static int tzabbr(const char *, time_t *);
static int tzoff(const char *, time_t *);

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
	tim = mktime(&tm);
	if (tim == -1) {
		warn("%s", str);
		return 1;
	}

	for (; *end == ' ' || *end == '\t'; end++)
		continue;
	if (tzparse(end, &tz)) {
		warnc(EINVAL, "tzparse: %s", str);
		return 1;
	}

	*res = tim - tz + env->tz_offset;
	return 0;
}

static const char *
timeparse(const char *str, struct tm *tm)
{
	static const char *formats[] = {
		"%a, %d %b %Y %H:%M:%S",
		"%a, %d %b %Y %H:%M",
		"%d %b %Y %H:%M:%S",
		NULL,
	};
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
tzparse(const char *str, time_t *tz)
{
	if (tzoff(str, tz) && tzabbr(str, tz))
		return 1;
	return 0;
}

static int
tzabbr(const char *str, time_t *tz)
{
	if (strcmp(str, "GMT") == 0) {
		*tz = 0;
		return 0;
	}

	return 1;
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
