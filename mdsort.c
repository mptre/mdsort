#include <err.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "extern.h"

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
	const struct match *match;
	struct message *msg;
	const char *confpath = NULL;
	int c;
	int dflag = 0;
	int nflag = 0;
	int verbose = 0;

	if (pledge("stdio rpath wpath cpath fattr getpw", NULL) == -1)
		err(1, "pledge");

	while ((c = getopt(argc, argv, "dnvf:")) != -1)
		switch (c) {
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
	log_init(verbose);

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
		md = maildir_open(conf->maildir, MAILDIR_WALK | MAILDIR_ROOT);
		if (md == NULL)
			continue;
		while (maildir_walk(md, path)) {
			msg = message_parse(path);
			if (msg == NULL)
				continue;

			match = expr_eval(conf->expr, msg);
			if (match == NULL) {
				message_free(msg);
				continue;
			}

			log_info("%s -> %s\n", path, match->path);
			if (dflag) {
				expr_inspect(conf->expr, stdout);
				message_free(msg);
				continue;
			}

			switch (match->action->type) {
			case EXPR_TYPE_FLAG:
			case EXPR_TYPE_MOVE:
				dst = maildir_open(match->path, 0);
				if (dst == NULL)
					break;
				(void)maildir_move(md, dst, msg, &env);
				maildir_close(dst);
				break;
			case EXPR_TYPE_DISCARD:
				(void)maildir_unlink(md, msg);
				break;
			default:
				break;
			}
			message_free(msg);
		}
		maildir_close(md);
	}

	while ((conf = TAILQ_FIRST(config)) != NULL) {
		TAILQ_REMOVE(config, conf, entry);
		expr_free(conf->expr);
		free(conf->maildir);
		free(conf);
	}

	return 0;
}

static __dead void
usage(void)
{
	fprintf(stderr, "usage: mdsort [-dnv] [-f file]\n");
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
