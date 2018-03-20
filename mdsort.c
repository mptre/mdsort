#include <err.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

const char *home, *hostname;

static void readenv(void);
static __dead void usage(void);

static const char *confpath;
static int verbose;

int
main(int argc, char *argv[])
{
	const struct config_list *config;
	struct config *conf;
	struct maildir *dst, *md;
	struct message *msg;
	struct rule *rl;
	struct rule_match *match;
	const char *path;
	char *str;
	size_t i, j;
	int c;
	int dflag = 0;
	int nflag = 0;

	if (pledge("stdio rpath wpath cpath fattr", NULL) == -1)
		err(1, "pledge");

	/* Extract mandatory data from the current environment. */
	readenv();

	while ((c = getopt(argc, argv, "dnvf:")) != -1)
		switch (c) {
		case 'd':
			dflag = 1;
			verbose = 2;
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

	config = parse_config(confpath);
	if (config == NULL)
		return 1;
	if (nflag)
		return 0;

	for (i = 0; i < config->nmemb; i++) {
		conf = config->list + i;
		md = maildir_open(conf->maildir, 0);
		if (md == NULL)
			continue;
		while ((path = maildir_walk(md)) != NULL) {
			msg = message_parse(path);
			if (msg == NULL)
				continue;
			for (j = 0; j < conf->nrules; j++) {
				rl = conf->rules[j];
				if ((match = rule_eval(rl, msg)) == NULL)
					continue;

				dst = maildir_openat(md, rule_get_dest(rl),
				    match);
				if (dst == NULL)
					continue;
				log_info("%s -> %s\n",
				    path, maildir_get_path(dst));
				if (dflag) {
					str = rule_match_str(match);
					if (str != NULL)
						log_info("%s", str);
					free(str);
				} else {
					maildir_move(md, dst, path);
				}
				maildir_close(dst);
				rule_match_free(match);
			}
			message_free(msg);
		}
		maildir_close(md);
	}

	for (i = 0; i < config->nmemb; i++) {
		conf = config->list + i;
		free(conf->maildir);
		for (j = 0; j < conf->nrules; j++)
			rule_free(conf->rules[j]);
		free(conf->rules);
	}
	free(config->list);

	return 0;
}

static __dead void
usage(void)
{
	fprintf(stderr, "usage: mdsort [-dnv] [-f file]\n");
	exit(1);
}

static void
readenv(void)
{
	static char confbuf[PATH_MAX], homebuf[PATH_MAX];
	static char hostbuf[HOST_NAME_MAX + 1];
	char *p;
	int n;

	if ((p = getenv("HOME")) != NULL && *p != '\0') {
		if (strlcpy(homebuf, p, PATH_MAX) >= PATH_MAX)
			errx(1, "%s: buffer too small", __func__);
	} else {
		/* XXX */
		errx(1, "HOME: not defined");
	}
	home = homebuf;

	if (gethostname(hostbuf, sizeof(hostbuf)) == -1)
		err(1, "gethostname");
	else if ((p = strchr(hostbuf, '.')) != NULL)
		*p = '\0';
	hostname = hostbuf;

	n = snprintf(confbuf, PATH_MAX, "%s/.mdsort.conf", home);
	if (n == -1 || n >= PATH_MAX)
		errx(1, "%s: buffer too small", __func__);
	confpath = confbuf;
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
