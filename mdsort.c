#include "config.h"

#include <assert.h>
#include <err.h>
#include <limits.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

const char *home = NULL;
const char *hostname = NULL;

static void readenv(void);
static __dead void usage(void);

static const char *confpath;
static int verbose;

int
main(int argc, char *argv[])
{
	struct config_list *config;
	struct config *conf;
	struct maildir *dst, *md;
	struct message *msg;
	const char *dstpath, *path;
	int c;
	int dflag = 0;
	int mdflags = MAILDIR_CREATE;
	int nflag = 0;

	if (pledge("stdio rpath wpath cpath fattr getpw", NULL) == -1)
		err(1, "pledge");

	while ((c = getopt(argc, argv, "Cdnvf:")) != -1)
		switch (c) {
		case 'C':
			mdflags &= ~(MAILDIR_CREATE);
			break;
		case 'd':
			dflag = 1;
			break;
		case 'f':
			confpath = optarg;
			break;
		case 'n':
			nflag = 1;
			break;
		case 'v':
			verbose++;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;
	if (argc > 0)
		usage();
	if (dflag && verbose < 1)
		verbose = 1;

	/* Extract mandatory data from the current environment. */
	readenv();

	if (pledge("stdio rpath wpath cpath fattr", NULL) == -1)
		err(1, "pledge");

	config = parse_config(confpath);
	if (config == NULL)
		return 1;
	if (nflag)
		return 0;

	TAILQ_FOREACH(conf, config, entry) {
		md = maildir_open(conf->maildir, mdflags | MAILDIR_WALK);
		if (md == NULL)
			continue;
		while ((path = maildir_walk(md)) != NULL) {
			msg = message_parse(path);
			if (msg == NULL)
				continue;

			dstpath = rule_eval(conf->rule, msg);
			if (dstpath == NULL)
				goto next;

			dst = maildir_open(dstpath, mdflags);
			if (dst == NULL)
				goto next;
			log_info("%s -> %s\n", path, dstpath);
			if (dflag)
				rule_inspect(conf->rule, stdout);
			else
				maildir_move(md, dst, path);
			maildir_close(dst);

next:
			message_free(msg);
		}
		maildir_close(md);
	}

	while ((conf = TAILQ_FIRST(config)) != NULL) {
		TAILQ_REMOVE(config, conf, entry);
		rule_free(conf->rule);
		free(conf->maildir);
		free(conf);
	}

	return 0;
}

static __dead void
usage(void)
{
	fprintf(stderr, "usage: mdsort [-Cdnv] [-f file]\n");
	exit(1);
}

static void
readenv(void)
{
	static char confbuf[PATH_MAX], homebuf[PATH_MAX];
	static char hostbuf[HOST_NAME_MAX + 1];
	struct passwd *pw;
	char *p;
	size_t len;
	int n;

	if (gethostname(hostbuf, sizeof(hostbuf)) == -1)
		err(1, "gethostname");
	else if ((p = strchr(hostbuf, '.')) != NULL)
		*p = '\0';
	hostname = hostbuf;

	if ((p = getenv("HOME")) == NULL || *p == '\0') {
		log_debug("%s: HOME: unset or empty\n", __func__);
		pw = getpwuid(getuid());
		if (pw != NULL)
			p = pw->pw_dir;
	}
	if (p == NULL)
		errx(1, "%s: cannot find home directory", __func__);
	if ((len = strlen(p) + 1) > sizeof(homebuf))
		errx(1, "%s: buffer too small", __func__);
	memcpy(homebuf, p, len);
	home = homebuf;

	if (confpath != NULL)
		return;
	n = snprintf(confbuf, PATH_MAX, "%s/.mdsort.conf", home);
	if (n == -1 || n >= PATH_MAX)
		errx(1, "%s: buffer too small", __func__);
	confpath = confbuf;
}

char *
pathjoin(char *buf, const char *root, const char *dirname, const char *filename)
{
	int n;

	assert(root != NULL);
	assert(dirname != NULL);

	if (filename == NULL)
		n = snprintf(buf, PATH_MAX, "%s/%s", root, dirname);
	else
		n = snprintf(buf, PATH_MAX, "%s/%s/%s",
		    root, dirname, filename);
	if (n == -1 || n >= PATH_MAX)
		errx(1, "%s: buffer too small", __func__);
	return buf;
}

void
log_debug(const char *fmt, ...)
{
	va_list ap;

	if (verbose < 2)
		return;
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
}

void
log_info(const char *fmt, ...)
{
	va_list ap;

	if (verbose < 1)
		return;
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
}
