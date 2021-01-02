#include "config.h"

#include <err.h>
#include <errno.h>
#include <fnmatch.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

#ifdef DIAGNOSTIC

struct fault {
	char fu_args[256];
	char fu_name[32];
	int fu_errno;
	unsigned int fu_hits;

	TAILQ_ENTRY(fault) fu_entry;
};

TAILQ_HEAD(, fault) faults = TAILQ_HEAD_INITIALIZER(faults);

static void fault_init(void);
static int fault_match(const char *, const char *);

static int fault_maildir_unlink(const char *);

static const char *parse_attr(struct fault *, const char *, const char *);

static int cold = 1;

int
fault(const char *name, ...)
{
	va_list ap;
	int error = 0;

	fault_init();

	va_start(ap, name);
	if (strcmp(name, "maildir_unlink") == 0)
		error = fault_maildir_unlink(va_arg(ap, const char *));
	else
		errx(1, "%s: %s: unknown fault", __func__, name);
	va_end(ap);

	return error;
}

void
fault_shutdown(void)
{
	struct fault *fu;
	int error = 0;

	fault_init();

	while ((fu = TAILQ_FIRST(&faults)) != NULL) {
		TAILQ_REMOVE(&faults, fu, fu_entry);
		if (fu->fu_hits == 0) {
			warnx("%s: %s: fault never hit", __func__, fu->fu_name);
			error = 1;
		}
		free(fu);
	}

	if (error)
		exit(66);
}

static void
fault_init(void)
{
	const char *str;

	if (cold == 0)
		return;

	str = getenv("FAULT");
	if (str == NULL || str[0] == '\0')
		return;
	log_debug("%s: faults=\"%s\"\n", __func__, str);

	for (;;) {
		struct fault *fu;
		const char *end, *p;

		p = end = strchr(str, ':');
		if (end == NULL)
			end = str + strlen(str);

		fu = calloc(1, sizeof(*fu));
		if (fu == NULL)
			err(1, NULL);

		for (;;) {
			str = parse_attr(fu, str, end);
			if (str == NULL)
				break;
		}
		TAILQ_INSERT_TAIL(&faults, fu, fu_entry);

		if (p == NULL)
			break;
		str = end + 1;
	}

	cold = 0;
}

static int
fault_match(const char *name, const char *args)
{
	struct fault *fu;

	TAILQ_FOREACH(fu, &faults, fu_entry) {
		if (strcmp(fu->fu_name, name))
			continue;
		if (fu->fu_args[0] != '\0' &&
		    fnmatch(fu->fu_args, args, 0) == FNM_NOMATCH)
			continue;
		if (fu->fu_hits > 0)
			continue;

		fu->fu_hits++;
		errno = fu->fu_errno;
		return 1;
	}
	return 0;
}

static int
fault_maildir_unlink(const char *path)
{
	return fault_match("maildir_unlink", path);
}

static const char *
parse_attr(struct fault *fu, const char *str, const char *end)
{
	const char *key, *val, *p;
	ssize_t siz;
	size_t keysiz, valsiz;
	int n;

	p = strchr(str, '=');
	if (p == NULL)
		errx(1, "%s: %s: invalid fault", __func__, str);
	key = str;
	keysiz = p - str;
	str = p + 1;

	p = strchr(str, ',');
	if (p == NULL)
		p = end;
	val = str;
	valsiz = p - str;
	str = p + 1;

	if (strncmp(key, "args", keysiz) == 0) {
		siz = sizeof(fu->fu_args);
		n = snprintf(fu->fu_args, siz, "%.*s", (int)valsiz, val);
		if (n < 0 || n >= siz)
			errc(1, ENAMETOOLONG, "%s", __func__);
	} else if (strncmp(key, "errno", keysiz) == 0) {
		if (strncmp(val, "ENOENT", valsiz) == 0) {
			fu->fu_errno = ENOENT;
		} else {
			errx(1, "%s: %.*s: unknown errno value",
			    __func__, (int)valsiz, val);
		}
	} else if (strncmp(key, "name", keysiz) == 0) {
		siz = sizeof(fu->fu_name);
		n = snprintf(fu->fu_name, siz, "%.*s", (int)valsiz, val);
		if (n < 0 || n >= siz)
			errc(1, ENAMETOOLONG, "%s", __func__);
	} else {
		errx(1, "%s: %.*s: unknown fault attribute",
		    __func__, (int)keysiz, key);
	}

	if (p == end)
		return NULL;
	return p + 1;
}

#endif	/* DIAGNOSTIC */