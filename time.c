#include "config.h"

#include <err.h>
#include <string.h>
#include <time.h>

#include "extern.h"

static int tzparse(const char *, time_t *);

int
time_parse(const char *str, time_t *res)
{
	struct tm tm;
	struct tm *tmp;
	const char *end;
	long gmtoff;
	time_t now, tim, tz;

	now = time(NULL);
	tmp = localtime(&now);
	gmtoff = tmp->tm_gmtoff;
	log_debug("%s: gmtoff=%ld\n", __func__, gmtoff);

	memset(&tm, 0, sizeof(tm));
	end = strptime(str, "%a, %d %b %Y %H:%M:%S", &tm);
	if (end == NULL) {
		warnx("%s: could not parse date", str);
		return 1;
	}
	tim = mktime(&tm);
	if (tim == -1) {
		warn("%s", str);
		return 1;
	}

	for (; *end == ' ' || *end == '\t'; end++)
		continue;
	if (tzparse(end, &tz)) {
		warnx("%s: could not parse timezone", str);
		return 1;
	}

	*res = tim - tz + gmtoff;
	return 0;
}

static int
tzparse(const char *str, time_t *tz)
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
