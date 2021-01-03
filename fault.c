#include "config.h"

#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

#ifdef DIAGNOSTIC

#define ARGS_MAX	1024

struct fault {
	char fu_args[ARGS_MAX];
	char fu_name[32];
	int fu_errno;
	unsigned int fu_hits;

	TAILQ_ENTRY(fault) fu_entry;
};

TAILQ_HEAD(, fault) faults = TAILQ_HEAD_INITIALIZER(faults);

static void fault_init(void);
static int fault_match(const char *);

static const char *parse_attr(struct fault *, const char *, const char *);

static int cold = 1;

int
fault(const char *name)
{
	fault_init();

	if (fault_match(name) == 0)
		return 0;
	warnx("fault: %s", name);
	return 1;
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
fault_match(const char *name)
{
	struct fault *fu;

	TAILQ_FOREACH(fu, &faults, fu_entry) {
		if (strcmp(fu->fu_name, name))
			continue;
		if (fu->fu_hits > 0)
			continue;

		fu->fu_hits++;
		errno = fu->fu_errno;
		return 1;
	}
	return 0;
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

	if (strncmp(key, "errno", keysiz) == 0) {
#define ERRNO(e) do {								\
		if (strncmp(val, #e, valsiz) == 0) {				\
			fu->fu_errno = e;					\
			goto out;						\
		}								\
} while (0)
		ERRNO(EINVAL);
		ERRNO(ENOENT);
		ERRNO(EXDEV);
#undef ERRNO

		errx(1, "%s: %.*s: unknown errno value",
		    __func__, (int)valsiz, val);
	} else if (strncmp(key, "name", keysiz) == 0) {
		siz = sizeof(fu->fu_name);
		n = snprintf(fu->fu_name, siz, "%.*s", (int)valsiz, val);
		if (n < 0 || n >= siz)
			errc(1, ENAMETOOLONG, "%s", __func__);
	} else {
		errx(1, "%s: %.*s: unknown fault attribute",
		    __func__, (int)keysiz, key);
	}

out:
	if (p == end)
		return NULL;
	return p + 1;
}

#endif	/* DIAGNOSTIC */
