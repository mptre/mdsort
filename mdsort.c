#include "config.h"

#include <err.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

int verbose = 0;

static const char *defaultconf(const struct environment *);
static void readenv(struct environment *);
static __dead void usage(void);

int
main(int argc, char *argv[])
{
	char path[PATH_MAX];
	struct environment env;
	struct config_list *config;
	struct config *conf;
	struct maildir *dst, *md;
	struct message *msg;
	const char *dstpath;
	const char *confpath = NULL;
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
	readenv(&env);

	if (pledge("stdio rpath wpath cpath fattr", NULL) == -1)
		err(1, "pledge");

	if (confpath == NULL)
		confpath = defaultconf(&env);
	config = parse_config(confpath, &env);
	if (config == NULL)
		return 1;
	if (nflag)
		return 0;

	TAILQ_FOREACH(conf, config, entry) {
		md = maildir_open(conf->maildir,
		    mdflags | MAILDIR_WALK | MAILDIR_ROOT);
		if (md == NULL)
			continue;
		while (maildir_walk(md, path)) {
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
				maildir_move(md, dst, msg, &env);
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

static const char *
defaultconf(const struct environment *env)
{
	static char buf[PATH_MAX];
	int len, n;

	len = sizeof(buf);
	n = snprintf(buf, len, "%s/.mdsort.conf", env->home);
	if (n == -1 || n >= len)
		errx(1, "%s: buffer too small", __func__);
	return buf;
}

static void
readenv(struct environment *env)
{
	struct passwd *pw;
	char *p;

	if (gethostname(env->hostname, sizeof(env->hostname)) == -1)
		err(1, "gethostname");
	else if ((p = strchr(env->hostname, '.')) != NULL)
		*p = '\0';

	if ((p = getenv("HOME")) == NULL || *p == '\0') {
		log_debug("%s: HOME: unset or empty\n", __func__);
		pw = getpwuid(getuid());
		if (pw != NULL)
			p = pw->pw_dir;
		else
			p = NULL;
	}
	if (p == NULL)
		errx(1, "%s: cannot find home directory", __func__);
	if (strlcpy(env->home, p, sizeof(env->home)) >= sizeof(env->home))
		errx(1, "%s: buffer too small", __func__);
}
